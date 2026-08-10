/*
 * TheXTech Dreamcast — boot progress probe (see boot_probe_dreamcast.cpp).
 */

#pragma once
#ifndef BOOT_PROBE_DREAMCAST_H
#define BOOT_PROBE_DREAMCAST_H

#include <cstdint>

// RGB565
#define DC_PROBE_BLUE    0x001Fu
#define DC_PROBE_GREEN   0x07E0u
#define DC_PROBE_RED     0xF800u
#define DC_PROBE_YELLOW  0xFFE0u
#define DC_PROBE_CYAN    0x07FFu
#define DC_PROBE_MAGENTA 0xF81Fu
#define DC_PROBE_WHITE   0xFFFFu

#ifdef THEXTECH_DC_BOOT_PROBE
void dc_boot_probe(uint16_t rgb565);

// Paints and then parks forever. Needed once the PVR is up, because it redraws
// the screen every frame and would wipe a plain marker.
void dc_boot_halt(uint16_t rgb565);
#else
static inline void dc_boot_probe(uint16_t) {}
static inline void dc_boot_halt(uint16_t) {}
#endif

#endif // BOOT_PROBE_DREAMCAST_H
