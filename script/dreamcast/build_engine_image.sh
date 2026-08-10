#!/usr/bin/env bash
# Package the TheXTech Dreamcast engine + converted assets into a bootable CDI.
#
# Run from the DreamSDK shell:
#   DREAMSDK_HOME='I:\sw\dc-dev\DreamSDK' bash.exe -l -c \
#       'bash /i/sw/TheXTech-main/script/dreamcast/build_engine_image.sh'
#
# Expects the asset tree to already exist (built on the host, where Python and
# Pillow live):
#   python utils/convertkit/gfx-convert-dc.py gamepack build-dreamcast/cdroot --worlds cliche
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build-dreamcast"
CDROOT="$OUT/cdroot"
ELF="${ELF:-$ROOT/build-dc/output/bin/thextech.elf}"
CDI="$OUT/thextech_dc.cdi"
ISO="$OUT/thextech_dc.iso"

VOLUME_NAME="${VOLUME_NAME:-THEXTECH}"
GAME_TITLE="${GAME_TITLE:-TheXTech}"

if [[ -z "${KOS_BASE:-}" ]]; then
  if [[ -f /opt/toolchains/dc/kos/environ.sh ]]; then
    # shellcheck disable=SC1091
    source /opt/toolchains/dc/kos/environ.sh
  else
    echo "KOS_BASE not set. Source KallistiOS environ.sh first." >&2
    exit 1
  fi
fi

[[ -f "$ELF" ]] || { echo "Engine ELF not found: $ELF" >&2; exit 1; }
[[ -d "$CDROOT" ]] || { echo "Asset tree not found: $CDROOT (run gfx-convert-dc.py first)" >&2; exit 1; }

SCRAMBLE="$KOS_BASE/utils/scramble/scramble"
[[ -x "$SCRAMBLE" ]] || SCRAMBLE="$SCRAMBLE.exe"
[[ -x "$SCRAMBLE" ]] || { echo "scramble not found under $KOS_BASE/utils" >&2; exit 1; }

echo "==> Converting ELF to raw binary"
sh-elf-objcopy -R .stack -O binary "$ELF" "$OUT/thextech.bin"
ls -lh "$OUT/thextech.bin"

echo "==> Scrambling to 1ST_READ.BIN"
"$SCRAMBLE" "$OUT/thextech.bin" "$CDROOT/1ST_READ.BIN"

# --- bootstrap header -------------------------------------------------------
IPBIN="$OUT/IP.BIN"
if [[ ! -f "$IPBIN" ]]; then
  echo "==> Generating IP.BIN"
  cat > "$OUT/ip.txt" <<EOF
Hardware ID   : SEGA SEGAKATANA
Maker ID      : SEGA ENTERPRISES
Device Info   : 0000 CD-ROM1/1
Area Symbols  : JUE
Peripherals   : E000F10
Product No    : T0000
Version       : V1.000
Release Date  : $(date +%Y%m%d)
Boot Filename : 1ST_READ.BIN
SW Maker Name : TheXTech Port
Game Title    : $GAME_TITLE
EOF
  MAKEIP="$KOS_BASE/utils/makeip/makeip"
  [[ -x "$MAKEIP" ]] || MAKEIP="$MAKEIP.exe"
  "$MAKEIP" "$OUT/ip.txt" "$IPBIN"
fi
[[ -f "$IPBIN" ]] || { echo "IP.BIN missing and could not be generated" >&2; exit 1; }

# --- ISO --------------------------------------------------------------------
# -C 0,11702 places the track where a Dreamcast expects the data session.
# -r -J -l gets us Rock Ridge + Joliet, both of which KOS's iso9660 driver
# understands, so ".dctex" names survive intact.
echo "==> Building ISO9660 image"
rm -f "$ISO"
mkisofs -C 0,11702 -V "$VOLUME_NAME" -G "$IPBIN" -r -J -l -o "$ISO" "$CDROOT"
ls -lh "$ISO"

# No -d here: that flag is for MSINFO 0 images. Ours is an MSINFO 11702
# (audio/data) layout, which cdi4dc expects with no flag at all.
echo "==> Converting to CDI"
rm -f "$CDI"
cdi4dc "$ISO" "$CDI"
ls -lh "$CDI"

echo
echo "Done. Boot image: $CDI"
