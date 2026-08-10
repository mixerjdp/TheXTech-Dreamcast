/*
 * TheXTech Dreamcast backend
 */

#pragma once
#ifndef PICTURE_DATA_DREAMCAST_H
#define PICTURE_DATA_DREAMCAST_H

#include <cstdint>

struct StdPicture;

// Textures are pre-converted on the host by utils/convertkit/gfx-convert-dc.py
// into PVR-ready blobs, so the Dreamcast never decodes PNG at runtime.
#define X_IMG_EXT ".dctex"
#define X_NO_PNG_GIF

/*!
 * \brief Dreamcast / PVR texture payload. Fields should not be used directly.
 *
 * Intentionally avoids including <kos.h> here: KOS macros (ERR_OK, CONT_X, …)
 * collide with TheXTech / third-party identifiers when pulled into every TU
 * via globals.h.
 */
struct StdPictureData
{
    void *texture = nullptr; // pvr_ptr_t
    uint16_t tex_w = 0;
    uint16_t tex_h = 0;
    uint32_t format = 0; // PVR_TXRFMT_* filled on upload
    int data_size = 0;

    // Multiply a half-res texel coordinate by these to get a PVR UV. They fold
    // in the power-of-two padding and, for oversized art that had to be shrunk
    // past half resolution, the extra downscale as well.
    float u_scale = 0.0f;
    float v_scale = 0.0f;

    StdPicture *last_texture = nullptr;
    StdPicture *next_texture = nullptr;
    uint32_t last_draw_frame = 0;

    inline bool hasTexture()
    {
        return texture != nullptr;
    }

    // Implemented in render_dreamcast.cpp (needs pvr_mem_free).
    void destroy();
};

#endif // PICTURE_DATA_DREAMCAST_H
