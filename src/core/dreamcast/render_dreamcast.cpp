/*
 * TheXTech Dreamcast backend — PVR renderer for 16 MB retail Dreamcast.
 *
 * Strategy:
 *  - Textures are pre-converted on the host into ".dctex" blobs (see
 *    utils/convertkit/gfx-convert-dc.py) so the console never decodes PNG:
 *    loading is a straight file->VRAM copy through the store queues.
 *  - Everything is submitted to the translucent list in draw order, which
 *    gives the painter's algorithm the engine expects.
 *  - Aggressive LRU eviction via render_minport_shared.
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

#include <kos.h>
#include <dc/pvr.h>
#include <dc/video.h>

#ifdef ERR_OK
#   undef ERR_OK
#endif

#include <Logger/logger.h>

#include "globals.h"
#include "config.h"
#include "frame_timer.h"
#include "core/window.h"
#include "core/render.h"
#include "core/render_planes.h"
#include "core/minport/render_minport_shared.hpp"

namespace XRender
{

// Forward-implemented minport hooks appear later in this translation unit.
void clearAllTextures();

bool g_in_frame = false;
static bool g_is_working = false;
static RenderPlanes_t s_render_planes;
static uint64_t s_last_frame_start = 0;

// The Dreamcast scans out 640x480; XWindow::getWindowSize reports twice that,
// as the minport layer expects (it halves whatever it is told).
static constexpr float s_output_w = 640.0f;
static constexpr float s_output_h = 480.0f;

// Half-res logical coordinates -> physical 640x480 pixels.
static float s_scale_x = 1.0f;
static float s_scale_y = 1.0f;
static float s_origin_x = 0.0f;
static float s_origin_y = 0.0f;

// ---------------------------------------------------------------------------
// .dctex — host-baked PVR texture blobs
// ---------------------------------------------------------------------------

#define DCTEX_MAGIC 0x58544344u // 'DCTX' little endian

struct DcTexHeader
{
    uint32_t magic;
    uint16_t tex_w, tex_h;   // power-of-two texture dimensions
    uint16_t span_w, span_h; // texels actually covered by the image
    uint16_t img_w, img_h;   // half-res size the engine addresses
    uint16_t fmt;            // 0 = ARGB4444, 1 = ARGB1555, 2 = RGB565
    uint16_t flags;          // bit0 = twiddled
    uint32_t reserved;
};

static_assert(sizeof(DcTexHeader) == 24, "unexpected .dctex header layout");

static uint32_t s_pvr_format(const DcTexHeader &h)
{
    uint32_t fmt;
    switch(h.fmt)
    {
    case 1:  fmt = PVR_TXRFMT_ARGB1555; break;
    case 2:  fmt = PVR_TXRFMT_RGB565;   break;
    default: fmt = PVR_TXRFMT_ARGB4444; break;
    }

    fmt |= (h.flags & 1) ? PVR_TXRFMT_TWIDDLED : PVR_TXRFMT_NONTWIDDLED;
    return fmt;
}

static bool s_read_header(const std::string &path, DcTexHeader &out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if(!f)
        return false;

    bool okay = std::fread(&out, 1, sizeof(out), f) == sizeof(out)
                && out.magic == DCTEX_MAGIC
                && out.tex_w > 0 && out.tex_h > 0;

    std::fclose(f);
    return okay;
}

static pvr_ptr_t s_vram_alloc(size_t bytes)
{
    pvr_ptr_t mem = pvr_mem_malloc(bytes);
    if(!mem)
    {
        // Drop everything not drawn in the last couple of frames and retry.
        minport_freeTextureMemory();
        mem = pvr_mem_malloc(bytes);
    }
    return mem;
}

/*!
 * \brief Streams a .dctex straight into VRAM.
 *
 * Copied in chunks so a 1024x1024 texture never needs a matching staging
 * buffer in the Dreamcast's 16 MB of main RAM.
 */
static bool s_load_dctex(StdPicture &target)
{
    alignas(32) static uint8_t s_chunk[16 * 1024];

    const std::string &path = target.l.path;

    FILE *f = std::fopen(path.c_str(), "rb");
    if(!f)
    {
        pLogWarning("Dreamcast: missing texture %s", path.c_str());
        return false;
    }

    DcTexHeader h;
    if(std::fread(&h, 1, sizeof(h), f) != sizeof(h) || h.magic != DCTEX_MAGIC)
    {
        pLogWarning("Dreamcast: bad .dctex header in %s", path.c_str());
        std::fclose(f);
        return false;
    }

    const size_t bytes = static_cast<size_t>(h.tex_w) * h.tex_h * 2;

    pvr_ptr_t mem = s_vram_alloc(bytes);
    if(!mem)
    {
        pLogWarning("Dreamcast: out of VRAM for %s (%u bytes, %u free)",
                    path.c_str(), (unsigned)bytes, (unsigned)pvr_mem_available());
        std::fclose(f);
        return false;
    }

    size_t done = 0;
    while(done < bytes)
    {
        size_t want = std::min(sizeof(s_chunk), bytes - done);
        size_t got = std::fread(s_chunk, 1, want, f);
        if(got == 0)
            break;

        // pvr_txr_load rounds up to a multiple of 4; pad the tail explicitly so
        // we never hand it uninitialised bytes.
        if(got & 3)
        {
            std::memset(s_chunk + got, 0, 4 - (got & 3));
            got += 4 - (got & 3);
        }

        pvr_txr_load(s_chunk, reinterpret_cast<pvr_ptr_t>(
                         reinterpret_cast<uintptr_t>(mem) + done), got);
        done += got;
    }

    std::fclose(f);

    if(done < bytes)
    {
        pLogWarning("Dreamcast: truncated texture %s (%u/%u)",
                    path.c_str(), (unsigned)done, (unsigned)bytes);
        pvr_mem_free(mem);
        return false;
    }

    target.d.destroy();
    target.d.texture = mem;
    target.d.tex_w = h.tex_w;
    target.d.tex_h = h.tex_h;
    target.d.format = s_pvr_format(h);
    target.d.data_size = static_cast<int>(bytes);

    // Half-res texel -> UV, folding in both the padding and any extra downscale
    // that oversized art needed to fit PVR's 1024 texel limit.
    target.d.u_scale = (h.img_w > 0)
        ? static_cast<float>(h.span_w) / (static_cast<float>(h.img_w) * h.tex_w)
        : 0.0f;
    target.d.v_scale = (h.img_h > 0)
        ? static_cast<float>(h.span_h) / (static_cast<float>(h.img_h) * h.tex_h)
        : 0.0f;

    return true;
}

// ---------------------------------------------------------------------------
// runtime-generated textures (rare: the engine hands us raw RGBA)
// ---------------------------------------------------------------------------

static uint32_t s_next_pow2(uint32_t v)
{
    uint32_t p = 8;
    while(p < v)
        p <<= 1;
    return p;
}

static inline uint32_t s_twidtab(uint32_t x)
{
    return (x & 1) | ((x & 2) << 1) | ((x & 4) << 2) | ((x & 8) << 3)
           | ((x & 16) << 4) | ((x & 32) << 5) | ((x & 64) << 6)
           | ((x & 128) << 7) | ((x & 256) << 8) | ((x & 512) << 9);
}

} // namespace XRender — reopen below after StdPictureData::destroy

void StdPictureData::destroy()
{
    if(texture)
    {
        pvr_mem_free(static_cast<pvr_ptr_t>(texture));
        texture = nullptr;
    }

    tex_w = 0;
    tex_h = 0;
    data_size = 0;
    u_scale = 0.0f;
    v_scale = 0.0f;
    last_draw_frame = 0;
}

namespace XRender
{

bool isWorking()
{
    return g_is_working;
}

bool hasFrameBuffer()
{
    return false;
}

bool init()
{
    pvr_init_params_t params = {
        // Only the translucent list is used, so the others get no bin space.
        { PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0 },
        512 * 1024, // vertex buffer
        0, // no DMA for maximum compatibility
        0, // no fsaa
        0, // autosort disabled -> strict submission (painter's) order
        0,
        0
    };

    if(pvr_init(&params) < 0)
    {
        pLogWarning("Dreamcast: pvr_init failed");
        return false;
    }

    vid_set_mode(DM_640x480, PM_RGB565);
    pvr_set_bg_color(0.0f, 0.0f, 0.0f);

    updateViewport();

    g_is_working = true;
    pLogInfo("Dreamcast PVR renderer ready (640x480, target %dx%d, %u KB VRAM free)",
             TargetW, TargetH, (unsigned)(pvr_mem_available() / 1024));
    return true;
}

void quit()
{
    if(g_is_working)
    {
        clearAllTextures();
        pvr_shutdown();
    }
    g_is_working = false;
}

void setTargetTexture()
{
    if(g_in_frame)
        return;

    pvr_wait_ready();
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_TR_POLY);

    minport_initFrame();
    s_render_planes.reset();
    g_in_frame = true;
}

void setTargetScreen()
{
}

void setDrawPlane(uint8_t plane)
{
    s_render_planes.set_plane(plane);
}

void clearBuffer()
{
    // The PVR background plane already paints every untouched pixel black.
    if(!g_in_frame)
        setTargetTexture();
}

void repaint()
{
    if(!g_in_frame)
        return;

    pvr_list_finish();
    pvr_scene_finish();

    g_in_frame = false;
    s_render_planes.reset();
}

void mapToScreen(int x, int y, int *dx, int *dy)
{
    *dx = static_cast<int>((x - g_screen_phys_x) * TargetW / static_cast<float>(g_screen_phys_w));
    *dy = static_cast<int>((y - g_screen_phys_y) * TargetH / static_cast<float>(g_screen_phys_h));
}

void mapFromScreen(int scr_x, int scr_y, int *window_x, int *window_y)
{
    *window_x = g_screen_phys_x + scr_x * g_screen_phys_w / TargetW;
    *window_y = g_screen_phys_y + scr_y * g_screen_phys_h / TargetH;
}

void lazyLoadPicture(StdPicture_Sub &target,
                     const std::string &path,
                     int scaleFactor,
                     const std::string &maskPath,
                     const std::string &maskFallbackPath)
{
    (void)maskPath;
    (void)maskFallbackPath;

    if(!GameIsActive)
        return;

    DcTexHeader h;
    if(!s_read_header(path, h))
    {
        pLogWarning("Dreamcast: cannot read texture header for %s", path.c_str());
        target.inited = false;
        return;
    }

    target.inited = true;
    target.l.path = path;
    target.l.lazyLoaded = true;

    // The engine works in full-res logical units; .dctex stores the half-res size.
    //
    // scaleFactor is deliberately ignored. It exists for callers handing us a
    // 1x source image that should be treated as larger — fonts pass their
    // "texture-scale" this way. Our blobs are pre-baked, so the header already
    // carries the final logical size, and applying the factor again would
    // double every font's glyph cell and render text as garbage. The Wii port
    // skips it for .tpl for exactly the same reason.
    (void)scaleFactor;
    target.w = h.img_w * 2;
    target.h = h.img_h * 2;
}

void lazyLoadPictureFromList(StdPicture_Sub &target,
                             PGE_FileFormats_misc::TextInput &t,
                             std::string &line_buf,
                             const std::string &dir)
{
    if(!GameIsActive)
        return;

    t.readLine(line_buf);
    if(line_buf.empty())
    {
        target.inited = false;
        return;
    }

    target.inited = true;
    target.l.path = dir + line_buf;
    target.l.lazyLoaded = true;

    int w = 0, h = 0;
    bool okay = false;
    t.readLine(line_buf);
    if(sscanf(line_buf.c_str(), "%d", &w) == 1)
    {
        t.readLine(line_buf);
        if(sscanf(line_buf.c_str(), "%d", &h) == 1)
            okay = true;
    }

    if(!okay || w <= 0 || h <= 0)
    {
        target.inited = false;
        return;
    }

    target.w = w;
    target.h = h;
}

void lazyLoad(StdPicture &target)
{
    if(!target.inited || target.d.hasTexture())
        return;

    if(!s_load_dctex(target))
    {
        // Back off for a while instead of hammering the GD-ROM every frame.
        target.d.last_draw_frame = g_current_frame + g_load_failure_retry_frames;
    }
}

void lazyPreLoad(StdPicture &target)
{
    lazyLoad(target);
}

void loadTexture(StdPicture &target, uint32_t width, uint32_t height, uint8_t *rgb, uint32_t pitch)
{
    target.inited = true;
    target.w = static_cast<int>(width);
    target.h = static_cast<int>(height);

    uint16_t tw = static_cast<uint16_t>(std::min<uint32_t>(1024, s_next_pow2(width)));
    uint16_t th = static_cast<uint16_t>(std::min<uint32_t>(1024, s_next_pow2(height)));

    size_t bytes = static_cast<size_t>(tw) * th * 2;
    pvr_ptr_t mem = s_vram_alloc(bytes);
    if(!mem)
        return;

    // Twiddle straight into VRAM: the same layout the host converter produces.
    volatile uint16_t *dst = reinterpret_cast<volatile uint16_t *>(mem);
    for(size_t i = 0; i < static_cast<size_t>(tw) * th; ++i)
        dst[i] = 0;

    const uint32_t mn = std::min<uint32_t>(tw, th);
    const uint32_t mask = mn - 1;

    for(uint32_t y = 0; y < height && y < th; ++y)
    {
        const uint8_t *src = rgb + y * pitch;
        for(uint32_t x = 0; x < width && x < tw; ++x)
        {
            uint16_t a = src[x * 4 + 3] >> 4;
            uint16_t r = src[x * 4 + 0] >> 4;
            uint16_t g = src[x * 4 + 1] >> 4;
            uint16_t b = src[x * 4 + 2] >> 4;

            uint32_t idx = (s_twidtab(y & mask) | (s_twidtab(x & mask) << 1))
                           + (x / mn + y / mn) * mn * mn;
            dst[idx] = static_cast<uint16_t>((a << 12) | (r << 8) | (g << 4) | b);
        }
    }

    target.d.destroy();
    target.d.texture = mem;
    target.d.tex_w = tw;
    target.d.tex_h = th;
    target.d.format = PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED;
    target.d.data_size = static_cast<int>(bytes);

    // Source coordinates arrive halved, so one texel spans two logical units.
    target.d.u_scale = 2.0f / tw;
    target.d.v_scale = 2.0f / th;
}

void unloadTexture(StdPicture &tx)
{
    minport_unlinkTexture(&tx);
    tx.d.destroy();
}

void clearAllTextures()
{
    while(g_render_chain_tail)
        unloadTexture(*g_render_chain_tail);
}

bool ready_for_frame()
{
    uint64_t now = timer_ms_gettime64();
    if(now - s_last_frame_start < 15)
        return false;
    s_last_frame_start = now;
    return true;
}

// ---------------------------------------------------------------------------
// minport hooks
// ---------------------------------------------------------------------------

static void s_update_transform()
{
    // The minport's own g_screen_phys_* would centre the game inside the output
    // at 1:1 — with the usual 800x600 target that is a 400x300 island in the
    // middle of the Dreamcast's 640x480, surrounded by untouched framebuffer.
    // Scale to fill the scanout instead.
    //
    // The factor is uniform, so a 4:3 target (which 800x600 and 1280x960 both
    // are, like the Dreamcast's own output) fills the screen exactly; anything
    // else letterboxes rather than stretching.
    const float half_w = (TargetW > 0) ? TargetW * 0.5f : 1.0f;
    const float half_h = (TargetH > 0) ? TargetH * 0.5f : 1.0f;

    float scale = s_output_w / half_w;
    if(half_h * scale > s_output_h)
        scale = s_output_h / half_h;

    s_scale_x = scale;
    s_scale_y = scale;

    float off_x = g_viewport_offset_ignore ? 0.0f : static_cast<float>(g_viewport_offset_x);
    float off_y = g_viewport_offset_ignore ? 0.0f : static_cast<float>(g_viewport_offset_y);

    s_origin_x = (s_output_w - half_w * scale) * 0.5f + (g_viewport_x + off_x) * scale;
    s_origin_y = (s_output_h - half_h * scale) * 0.5f + (g_viewport_y + off_y) * scale;
}

static void minport_TransformPhysCoords()
{
}

static void minport_ApplyPhysCoords()
{
    s_update_transform();
}

static void minport_ApplyViewport()
{
    // NOTE: no scissor yet. PVR clipping is 32x32 tile granular and the engine
    // only needs a sub-viewport for split-screen, which the port doesn't run.
    s_update_transform();
}

static inline float s_px(float x)
{
    return s_origin_x + x * s_scale_x;
}

static inline float s_py(float y)
{
    return s_origin_y + y * s_scale_y;
}

static void minport_RenderBoxFilled(int x1, int y1, int x2, int y2, XTColor color)
{
    if(color.a == 0)
        return;

    float z = static_cast<float>(s_render_planes.next());

    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    uint32_t argb = PVR_PACK_COLOR(color.a / 255.0f, color.r / 255.0f,
                                   color.g / 255.0f, color.b / 255.0f);

    pvr_vertex_t vert;
    auto emit = [&](float x, float y, uint32_t flags)
    {
        vert.flags = flags;
        vert.x = s_px(x);
        vert.y = s_py(y);
        vert.z = z;
        vert.u = 0.0f;
        vert.v = 0.0f;
        vert.argb = argb;
        vert.oargb = 0;
        pvr_prim(&vert, sizeof(vert));
    };

    emit(static_cast<float>(x1), static_cast<float>(y1), PVR_CMD_VERTEX);
    emit(static_cast<float>(x2), static_cast<float>(y1), PVR_CMD_VERTEX);
    emit(static_cast<float>(x1), static_cast<float>(y2), PVR_CMD_VERTEX);
    emit(static_cast<float>(x2), static_cast<float>(y2), PVR_CMD_VERTEX_EOL);
}

static void s_draw_textured(StdPicture &tx,
                            int16_t xDst, int16_t yDst, int16_t wDst, int16_t hDst,
                            int16_t xSrc, int16_t ySrc, int16_t wSrc, int16_t hSrc,
                            unsigned int flip,
                            XTColor color)
{
    if(!tx.inited)
        return;

    if(!tx.d.hasTexture() && tx.l.lazyLoaded)
        lazyLoad(tx);

    if(!tx.d.hasTexture())
        return;

    minport_usedTexture(tx);

    float z = static_cast<float>(s_render_planes.next());

    float u0 = xSrc * tx.d.u_scale;
    float v0 = ySrc * tx.d.v_scale;
    float u1 = (xSrc + wSrc) * tx.d.u_scale;
    float v1 = (ySrc + hSrc) * tx.d.v_scale;

    if(flip & X_FLIP_HORIZONTAL)
        std::swap(u0, u1);
    if(flip & X_FLIP_VERTICAL)
        std::swap(v0, v1);

    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, tx.d.format, tx.d.tex_w, tx.d.tex_h,
                     static_cast<pvr_ptr_t>(tx.d.texture), PVR_FILTER_NONE);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    uint32_t argb = PVR_PACK_COLOR(color.a / 255.0f, color.r / 255.0f,
                                   color.g / 255.0f, color.b / 255.0f);

    pvr_vertex_t vert;
    auto emit = [&](float x, float y, float u, float v, uint32_t flags)
    {
        vert.flags = flags;
        vert.x = s_px(x);
        vert.y = s_py(y);
        vert.z = z;
        vert.u = u;
        vert.v = v;
        vert.argb = argb;
        vert.oargb = 0;
        pvr_prim(&vert, sizeof(vert));
    };

    emit(static_cast<float>(xDst), static_cast<float>(yDst), u0, v0, PVR_CMD_VERTEX);
    emit(static_cast<float>(xDst + wDst), static_cast<float>(yDst), u1, v0, PVR_CMD_VERTEX);
    emit(static_cast<float>(xDst), static_cast<float>(yDst + hDst), u0, v1, PVR_CMD_VERTEX);
    emit(static_cast<float>(xDst + wDst), static_cast<float>(yDst + hDst), u1, v1, PVR_CMD_VERTEX_EOL);
}

static void minport_RenderTexturePrivate(int16_t xDst, int16_t yDst, int16_t wDst, int16_t hDst,
                                         StdPicture &tx,
                                         int16_t xSrc, int16_t ySrc, int16_t wSrc, int16_t hSrc,
                                         int16_t rotateAngle, Point_t *center, unsigned int flip,
                                         XTColor color)
{
    // Rotation is only used by a handful of effects; skipping it keeps the
    // vertex path branch-free on SH4.
    (void)rotateAngle;
    (void)center;
    s_draw_textured(tx, xDst, yDst, wDst, hDst, xSrc, ySrc, wSrc, hSrc, flip, color);
}

static void minport_RenderTexturePrivate_Basic(int16_t xDst, int16_t yDst, int16_t wDst, int16_t hDst,
                                               StdPicture &tx,
                                               int16_t xSrc, int16_t ySrc,
                                               XTColor color)
{
    s_draw_textured(tx, xDst, yDst, wDst, hDst, xSrc, ySrc, wDst, hDst, X_FLIP_NONE, color);
}

} // namespace XRender
