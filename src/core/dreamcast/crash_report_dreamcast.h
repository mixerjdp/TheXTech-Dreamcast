/*
 * TheXTech Dreamcast — see crash_report_dreamcast.cpp.
 */

#pragma once
#ifndef CRASH_REPORT_DREAMCAST_H
#define CRASH_REPORT_DREAMCAST_H

#ifdef THEXTECH_DC_BOOT_PROBE
//! Paints the faulting PC on screen instead of freezing silently.
void dc_install_crash_handler();
#else
static inline void dc_install_crash_handler() {}
#endif

#endif // CRASH_REPORT_DREAMCAST_H
