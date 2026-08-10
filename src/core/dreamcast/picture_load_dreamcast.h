/*
 * TheXTech Dreamcast backend
 */

#pragma once
#ifndef STD_PICTURE_LOAD_DREAMCAST_H
#define STD_PICTURE_LOAD_DREAMCAST_H

#include <string>
#include <cstdint>

struct StdPictureLoad
{
    bool lazyLoaded = false;
    std::string path = "";
    std::string mask_path = "";

    bool colorKey = false;
    uint8_t keyRgb[3] = {0, 0, 0};

    inline bool canLoad() const
    {
        return lazyLoaded;
    }
};

#endif
