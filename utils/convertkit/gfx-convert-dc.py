#!/usr/bin/python3
"""
Converts TheXTech assets for the Sega Dreamcast (KallistiOS / PVR) port.

Images (PNG / GIF + SMBX mask) become ".dctex": a PVR-ready blob that the
runtime can upload to VRAM with no decoding and almost no scratch RAM.

Like the Wii pipeline, textures are stored at half resolution: the engine's
minport layer addresses texels in half-res space (see FLOORDIV2 in
render_minport_shared.hpp) while the ".size" sidecar reports full-res
dimensions.

.dctex layout (little endian, 24 byte header):
     0  char[4] magic "DCTX"
     4  u16     tex_w     power-of-two texture width  (<= 1024)
     6  u16     tex_h     power-of-two texture height (<= 1024)
     8  u16     span_w    texels actually covered by the image
    10  u16     span_h
    12  u16     img_w     half-res logical width the engine addresses
    14  u16     img_h
    16  u16     fmt       0=ARGB4444 1=ARGB1555 2=RGB565
    18  u16     flags     bit0 = twiddled
    20  u32     reserved
    24  pixel data, tex_w * tex_h * 2 bytes

span_* differs from img_* only for oversized art that had to be shrunk past
half resolution to fit PVR's 1024 texel limit; the runtime turns the ratio
into a UV scale, so the engine never needs to know.
"""

import argparse
import configparser
import os
import shutil
import struct
import subprocess
import sys

import numpy as np
from PIL import Image

MAGIC = b'DCTX'
HEADER_SIZE = 24

FMT_ARGB4444 = 0
FMT_ARGB1555 = 1
FMT_RGB565 = 2

PVR_MAX_DIM = 1024
PVR_MIN_DIM = 8

IMAGE_EXTS = ('.png', '.gif')
AUDIO_EXTS = ('.ogg', '.mp3', '.wav', '.spc', '.mid', '.xm', '.it', '.mod', '.s3m', '.flac')

# Everything the engine may list as music; ffmpeg reads the chiptune ones
# through its libgme demuxer.
MUSIC_EXTS = ('.spc', '.nsf', '.it', '.ogg', '.mod', '.xm', '.s3m', '.mid', '.mp3', '.flac')

# Chiptunes never end on their own; cap them and let the Dreamcast mixer
# reopen the file when it hits EOF (sndoggvorbis seeks on /cd are unreliable).
MUSIC_SECONDS = 120

# Prefixes the engine can batch-load through graphics.list
LIST_PREFIXES = (
    'background', 'background2', 'block', 'effect', 'level',
    'link', 'luigi', 'mario', 'npc', 'path',
    'peach', 'player', 'scene', 'tile', 'toad',
    'yoshib', 'yoshit',
)


# --------------------------------------------------------------------------
# twiddling (mirrors pvr_txr_load_ex, 16bpp case, in KallistiOS)
# --------------------------------------------------------------------------

def _twiddle_table(n):
    """TWIDTAB: spread the bits of 0..n-1 so they interleave with a partner."""
    x = np.arange(n, dtype=np.uint32)
    out = np.zeros(n, dtype=np.uint32)
    for bit in range(10):
        out |= (x & (1 << bit)) << bit
    return out


_twiddle_cache = {}


def twiddle_indices(w, h):
    """Destination index for every source pixel of a linear w*h image."""
    key = (w, h)
    if key in _twiddle_cache:
        return _twiddle_cache[key]

    mn = min(w, h)
    mask = mn - 1

    xs = np.arange(w, dtype=np.uint32)
    ys = np.arange(h, dtype=np.uint32)

    tab_x = _twiddle_table(w)[xs & mask]
    tab_y = _twiddle_table(h)[ys & mask]

    # TWIDOUT(x, y) = TWIDTAB(y) | (TWIDTAB(x) << 1)
    inner = tab_y[:, None] | (tab_x[None, :] << 1)
    block = ((xs[None, :] // mn) + (ys[:, None] // mn)) * (mn * mn)

    idx = (inner + block).astype(np.uint32)
    if len(_twiddle_cache) < 64:
        _twiddle_cache[key] = idx
    return idx


def twiddle(pixels16):
    """pixels16: (h, w) uint16 linear -> flat uint16 twiddled."""
    h, w = pixels16.shape
    out = np.empty(w * h, dtype=np.uint16)
    out[twiddle_indices(w, h).ravel()] = pixels16.ravel()
    return out


# --------------------------------------------------------------------------
# pixel packing
# --------------------------------------------------------------------------

def pick_format(rgba):
    """Choose the cheapest 16bpp format that preserves this image."""
    a = rgba[:, :, 3]
    if np.all(a == 255):
        return FMT_RGB565
    if np.all((a == 0) | (a == 255)):
        return FMT_ARGB1555
    return FMT_ARGB4444


def pack(rgba, fmt):
    r = rgba[:, :, 0].astype(np.uint16)
    g = rgba[:, :, 1].astype(np.uint16)
    b = rgba[:, :, 2].astype(np.uint16)
    a = rgba[:, :, 3].astype(np.uint16)

    if fmt == FMT_RGB565:
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    if fmt == FMT_ARGB1555:
        return ((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    return ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)


def next_pow2(v):
    p = PVR_MIN_DIM
    while p < v:
        p <<= 1
    return p


# --------------------------------------------------------------------------
# image loading
# --------------------------------------------------------------------------

def load_rgba(path, mask_path=None):
    """Load an image, applying an SMBX mask (white = transparent) if given."""
    with Image.open(path) as im:
        im.seek(0)  # animated GIFs: first frame only, like the Wii pipeline
        img = im.convert('RGBA')

    if mask_path and os.path.isfile(mask_path):
        with Image.open(mask_path) as mk:
            mk.seek(0)
            mask = mk.convert('L')
        if mask.size != img.size:
            mask = mask.resize(img.size, Image.Resampling.NEAREST)
        # SMBX masks are inverted: white hides, black shows.
        alpha = 255 - np.asarray(mask, dtype=np.uint8)
        arr = np.asarray(img, dtype=np.uint8).copy()
        arr[:, :, 3] = alpha
        return arr

    return np.asarray(img, dtype=np.uint8).copy()


def find_mask(path):
    """SMBX keeps a '<name>m.gif' beside '<name>.gif' / '<name>.png'."""
    stem, ext = os.path.splitext(path)
    cand = stem + 'm.gif'
    if os.path.isfile(cand):
        return cand

    # ...or in the shared fallback directory
    d, fn = os.path.split(stem)
    parent, sub = os.path.split(d)
    if sub and sub != 'fallback':
        cand = os.path.join(parent, 'fallback', fn + 'm.gif')
        if os.path.isfile(cand):
            return cand
    return None


def convert_image(src, dst_dctex, twiddled=True, half=True):
    """Returns (full_res_w, full_res_h) or None when the image is unusable.

    `half` stores the image at half resolution, which is what the engine's
    half-res texel space expects for ordinary graphics. Font atlases declaring
    "texture-scale = 2" must be kept at full resolution instead, exactly as the
    Wii pipeline does, or every glyph samples from the wrong texels.
    """
    rgba = load_rgba(src, find_mask(src))
    h, w = rgba.shape[0], rgba.shape[1]
    if w <= 0 or h <= 0:
        return None

    if half:
        img_w = max(1, round(w / 2))
        img_h = max(1, round(h / 2))
    else:
        img_w, img_h = w, h

    # Oversized art gets shrunk further; the runtime compensates via UV scale.
    span_w, span_h = img_w, img_h
    while span_w > PVR_MAX_DIM or span_h > PVR_MAX_DIM:
        span_w = max(1, span_w // 2)
        span_h = max(1, span_h // 2)

    if (span_w, span_h) != (w, h):
        pil = Image.fromarray(rgba, 'RGBA').resize(
            (span_w, span_h), Image.Resampling.NEAREST)
        rgba = np.asarray(pil, dtype=np.uint8).copy()

    tex_w = next_pow2(span_w)
    tex_h = next_pow2(span_h)

    fmt = pick_format(rgba)
    if (tex_w, tex_h) != (span_w, span_h) and fmt == FMT_RGB565:
        # padding must be transparent, so we need an alpha channel after all
        fmt = FMT_ARGB1555

    packed = pack(rgba, fmt).astype(np.uint16)

    if (tex_w, tex_h) != (span_w, span_h):
        canvas = np.zeros((tex_h, tex_w), dtype=np.uint16)
        canvas[:span_h, :span_w] = packed
        packed = canvas

    data = twiddle(packed) if twiddled else packed.ravel()

    header = struct.pack(
        '<4sHHHHHHHHI', MAGIC,
        tex_w, tex_h, span_w, span_h, img_w, img_h,
        fmt, 1 if twiddled else 0, 0)

    with open(dst_dctex, 'wb') as f:
        f.write(header)
        f.write(data.astype('<u2').tobytes())

    return img_w * 2, img_h * 2


def write_size(path, w, h):
    # Fixed 10-byte layout the runtime parses with atoi()
    with open(path, 'w') as f:
        f.write(f'{w:>4}\n{h:>4}\n')


# --------------------------------------------------------------------------
# graphics.list generation
# --------------------------------------------------------------------------

def list_entry_name(filename):
    """'npc-10.dctex.size' -> ('npc 10', 'npc-10.dctex'), or None."""
    base = filename[:filename.find('.')]
    parts = base.split('-')
    if len(parts) != 2 or not parts[1].isdigit():
        return None
    if parts[0].lower() not in LIST_PREFIXES:
        return None
    return base.replace('-', ' ').lower(), filename[:-len('.size')]


def build_lists(outdir):
    """Fold .size sidecars into graphics.list files, as the Wii port does."""
    made = 0
    for dirpath, dirs, files in os.walk(outdir, topdown=True):
        entries = []

        if os.path.basename(dirpath) == 'graphics':
            # engine expects one list covering every graphics subfolder
            for d in sorted(dirs):
                if d in ('touchscreen', 'ui', 'fallback'):
                    continue
                sub = os.path.join(dirpath, d)
                for fn in sorted(os.listdir(sub)):
                    if not fn.endswith('.size'):
                        continue
                    named = list_entry_name(fn)
                    if not named:
                        continue
                    label, texname = named
                    entries.append((label, os.path.join(d, texname).replace('\\', '/'),
                                    os.path.join(sub, fn)))
        elif 'graphics' in dirpath.replace('\\', '/').split('/'):
            continue  # handled by the parent 'graphics' pass
        else:
            for fn in sorted(files):
                if not fn.endswith('.size'):
                    continue
                named = list_entry_name(fn)
                if not named:
                    continue
                label, texname = named
                entries.append((label, texname, os.path.join(dirpath, fn)))

        if not entries:
            continue

        with open(os.path.join(dirpath, 'graphics.list'), 'w') as l:
            for label, texname, sizefile in entries:
                with open(sizefile) as sf:
                    dims = sf.read()
                l.write(label + '\n')
                l.write(texname + '\n')
                l.write(dims)
                l.write('\n')
                os.remove(sizefile)
        made += 1
    return made


# --------------------------------------------------------------------------
# main walk
# --------------------------------------------------------------------------

def fonts_kept_at_full_res(dirpath, files):
    """Font atlases whose .ini declares a texture-scale other than 1.

    Those are addressed at their own resolution by the font code, so unlike
    every other graphic they must not be halved.
    """
    keep = set()

    for fn in files:
        if not fn.endswith('.ini'):
            continue

        font = configparser.ConfigParser(inline_comment_prefixes=';',
                                         strict=False)
        try:
            font.read(os.path.join(dirpath, fn), encoding='utf-8')
        except Exception:                                    # noqa: BLE001
            continue

        if 'font-map' not in font:
            continue

        section = font['font-map']
        scale = section.get('texture-scale', '1').strip()
        texture = section.get('texture', '').strip().strip('"').strip()

        if scale != '1' and texture:
            keep.add(texture)

    return keep


def convert_sound(src, dst):
    """Transcode a sound effect to Yamaha ADPCM WAV for the AICA.

    That is one of the formats KallistiOS's snd_sfx_load reads natively, and at
    4 bits per sample the gamepack's ~100 effects fit comfortably in the AICA's
    2 MB of sound RAM. Mono at 22 kHz: the Dreamcast plays these centred anyway.
    """
    result = subprocess.run(
        ['ffmpeg', '-y', '-loglevel', 'error', '-i', src,
         '-ac', '1', '-ar', '22050', '-acodec', 'adpcm_yamaha', dst],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode('utf-8', 'replace').strip()[:200])


def convert_music(src, dst):
    """Transcode a track to mono 22 kHz Ogg Vorbis.

    Everything becomes Ogg because that is what sndoggvorbis can stream, and
    mono at 22 kHz keeps tremor's integer decode cheap enough to run beside the
    game on a 200 MHz SH4. SPC/NSF/IT come in through ffmpeg's libgme demuxer.

    Also writes dst + '.size' (ASCII byte length). The Dreamcast mixer needs an
    authoritative length so a false GD-ROM EOF can't leave a ~4 s stub on /ram.
    """
    result = subprocess.run(
        ['ffmpeg', '-y', '-loglevel', 'error', '-i', src,
         '-t', str(MUSIC_SECONDS), '-ac', '1', '-ar', '22050',
         '-c:a', 'libvorbis', '-b:a', '24k', dst],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

    if result.returncode != 0:
        raise RuntimeError(result.stderr.decode('utf-8', 'replace').strip()[:200])

    with open(dst + '.size', 'w', encoding='ascii') as f:
        f.write(str(os.path.getsize(dst)))


def rewrite_music_ini(src, dst):
    """Point music.ini at the converted tracks.

    Entries look like  file="smb3-world1.spc|0;g=2.7"  — the tail after "|" is
    GME configuration that means nothing once the track is Ogg, so it goes too.
    """
    import re as _re

    with open(src, 'rb') as f:
        data = f.read().decode('utf-8', 'surrogateescape')

    def fix(m):
        name = m.group(1).split('|', 1)[0]
        return 'file="%s.ogg"' % os.path.splitext(name)[0]

    data = _re.sub(r'file="([^"]+)"', fix, data)

    with open(dst, 'wb') as f:
        f.write(data.encode('utf-8', 'surrogateescape'))


def rewrite_level_music_ext(src, dst):
    """Retarget embedded music paths in level/world files to .ogg.

    PGE-X sections use MF:"music/track.spc" or MF:"music/track.spc|0;g=…".
    After conversion only the .ogg exists, so the extension (and any GME
    tail) must be rewritten or Mix_LoadMUS fails and the section is silent.
    """
    import re as _re

    with open(src, 'rb') as f:
        data = f.read().decode('utf-8', 'surrogateescape')

    def fix(m):
        path = m.group(1).split('|', 1)[0]
        stem, ext = os.path.splitext(path)
        if ext.lower() in MUSIC_EXTS:
            path = stem + '.ogg'
        return 'MF:"%s"' % path

    data = _re.sub(r'MF:"([^"]*)"', fix, data)

    with open(dst, 'wb') as f:
        f.write(data.encode('utf-8', 'surrogateescape'))


def rewrite_ext_ini(src, dst, old_ext, new_ext):
    """Rewrite an ini's asset extension, as bytes.

    These files carry arbitrary encodings and must survive untouched apart from
    the extension.
    """
    with open(src, 'rb') as f:
        data = f.read()
    data = data.replace(old_ext, new_ext)
    with open(dst, 'wb') as f:
        f.write(data)


def rewrite_font_ini(src, dst):
    """Font descriptors point at .png textures; retarget them to .dctex.

    Handled as bytes: these files carry arbitrary encodings and must survive
    the rewrite untouched apart from the extension.
    """
    with open(src, 'rb') as f:
        data = f.read()
    data = data.replace(b'.png', b'.dctex')
    with open(dst, 'wb') as f:
        f.write(data)


def main():
    ap = argparse.ArgumentParser(
        description='Convert TheXTech assets for the Sega Dreamcast port')
    ap.add_argument('input')
    ap.add_argument('output')
    ap.add_argument('--worlds', nargs='*', default=None,
                    help='only convert these worlds (default: all)')
    ap.add_argument('--no-sfx', action='store_true',
                    help='skip converting sound effects (needs ffmpeg)')
    ap.add_argument('--no-music', action='store_true',
                    help='skip converting music (needs ffmpeg with libgme)')
    ap.add_argument('--no-twiddle', action='store_true',
                    help='store linear textures instead of twiddled ones')
    ap.add_argument('--no-lists', action='store_true',
                    help='skip graphics.list generation')
    args = ap.parse_args()

    indir = os.path.abspath(args.input)
    outdir = os.path.abspath(args.output)
    twiddled = not args.no_twiddle

    if not os.path.isdir(indir):
        sys.exit(f'input directory not found: {indir}')

    os.makedirs(outdir, exist_ok=True)

    n_img = n_copy = n_skip = n_fail = n_sfx = n_mus = 0

    for dirpath, dirs, files in os.walk(indir, topdown=True):
        rel = os.path.relpath(dirpath, indir)
        rel = '' if rel == '.' else rel
        parts = rel.replace('\\', '/').split('/') if rel else []

        # hidden dirs are never content
        dirs[:] = [d for d in dirs if not d.startswith('.')]

        if args.worlds is not None and len(parts) >= 2 and parts[0] == 'worlds':
            if parts[1] not in args.worlds:
                dirs[:] = []
                continue

        if args.no_music and parts and parts[0] == 'music':
            dirs[:] = []
            continue

        outpath = os.path.join(outdir, rel) if rel else outdir
        os.makedirs(outpath, exist_ok=True)

        is_fonts = os.path.basename(dirpath) == 'fonts'
        full_res = fonts_kept_at_full_res(dirpath, files) if is_fonts else set()

        for fn in sorted(files):
            src = os.path.join(dirpath, fn)
            if not os.path.isfile(src):
                continue

            low = fn.lower()
            stem, ext = os.path.splitext(fn)

            # masks are folded into their base image
            if low.endswith('m.gif'):
                base_png = os.path.join(dirpath, stem[:-1] + '.png')
                base_gif = os.path.join(dirpath, stem[:-1] + '.gif')
                if os.path.isfile(base_png) or os.path.isfile(base_gif):
                    n_skip += 1
                    continue

            if low.endswith(IMAGE_EXTS):
                dctex = os.path.join(outpath, stem + '.dctex')
                try:
                    dims = convert_image(src, dctex, twiddled,
                                         half=(fn not in full_res))
                except Exception as e:                       # noqa: BLE001
                    print(f'  !! {src}: {e}')
                    n_fail += 1
                    continue
                if dims is None:
                    n_fail += 1
                    continue
                write_size(dctex + '.size', dims[0], dims[1])
                n_img += 1
                if n_img % 500 == 0:
                    print(f'  ... {n_img} textures')
                continue

            # Top-level music/ AND episode music/ (e.g. worlds/cliche/music/).
            # Without this, level MF: paths like music/mrpg-….spc are missing
            # on the CDI and the section plays in silence.
            in_music_dir = bool(parts) and (
                parts[0] == 'music' or parts[-1] == 'music')

            if in_music_dir and low.endswith(MUSIC_EXTS):
                if args.no_music:
                    n_skip += 1
                    continue

                ogg = os.path.join(outpath, stem + '.ogg')
                try:
                    convert_music(src, ogg)
                except Exception as e:                       # noqa: BLE001
                    print(f'  !! {src}: {e}')
                    n_fail += 1
                    continue
                n_mus += 1
                if n_mus % 20 == 0:
                    print(f'  ... {n_mus} pistas')
                continue

            if low == 'music.ini' and not args.no_music:
                rewrite_music_ini(src, os.path.join(outpath, fn))
                n_copy += 1
                continue

            if low.endswith(('.lvlx', '.wldx', '.lvl', '.wld')) and not args.no_music:
                # Section/world music paths embed the original extension
                # (MF:"music/foo.spc"); retarget to the converted .ogg.
                rewrite_level_music_ext(src, os.path.join(outpath, fn))
                n_copy += 1
                continue

            if low.endswith(AUDIO_EXTS):
                is_sfx = bool(parts) and parts[0] == 'sound'

                if is_sfx and not args.no_sfx:
                    wav = os.path.join(outpath, stem + '.wav')
                    try:
                        convert_sound(src, wav)
                    except Exception as e:                   # noqa: BLE001
                        print(f'  !! {src}: {e}')
                        n_fail += 1
                        continue
                    n_sfx += 1
                    continue

                n_skip += 1
                continue

            # sounds.ini names the effects by their original extension
            if low == 'sounds.ini' and not args.no_sfx:
                rewrite_ext_ini(src, os.path.join(outpath, fn), b'.ogg', b'.wav')
                n_copy += 1
                continue

            if low == 'thumbs.db' or low.endswith('.db'):
                n_skip += 1
                continue

            if is_fonts and low.endswith('.ini'):
                rewrite_font_ini(src, os.path.join(outpath, fn))
                n_copy += 1
                continue

            shutil.copy2(src, os.path.join(outpath, fn))
            n_copy += 1

    print(f'textures: {n_img}  sfx: {n_sfx}  music: {n_mus}  copied: {n_copy}  '
          f'skipped: {n_skip}  failed: {n_fail}')

    if not args.no_lists:
        made = build_lists(outdir)
        print(f'graphics.list files written: {made}')

    total = 0
    for dirpath, _, files in os.walk(outdir):
        for f in files:
            total += os.path.getsize(os.path.join(dirpath, f))
    print(f'output size: {total / (1024 * 1024):.1f} MiB  ->  {outdir}')


if __name__ == '__main__':
    main()
