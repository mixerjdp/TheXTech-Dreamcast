/*
 * TheXTech Dreamcast — boot progress probe.
 *
 * The Dreamcast has no console we can read back from Flycast, so early-boot
 * faults are otherwise invisible. When enabled, this paints the raw framebuffer
 * at known points so a screenshot tells us how far startup got:
 *
 *   black  — died before/inside the earliest constructor
 *   blue   — earliest constructor ran; died in the remaining static ctors
 *   green  — reached main()
 *
 * Disabled by default. Enable with -DTHEXTECH_DC_BOOT_PROBE=ON, and note that
 * seeing the colours in Flycast also needs its "emulate framebuffer" option,
 * since these are direct VRAM writes rather than PVR draws.
 */

#include "core/dreamcast/boot_probe_dreamcast.h"

#ifdef THEXTECH_DC_BOOT_PROBE

#include <kos.h>
#include <dc/video.h>

#ifdef ERR_OK
#   undef ERR_OK
#endif

void dc_boot_probe(uint16_t rgb565)
{
    static bool s_video_ready = false;

    if(!s_video_ready)
    {
        vid_set_mode(DM_640x480, PM_RGB565);
        s_video_ready = true;
    }

    for(int i = 0; i < 640 * 480; ++i)
        vram_s[i] = rgb565;

#ifdef THEXTECH_DC_PROBE_HALT
    // Park here so the marker cannot be overwritten by a later stage.
    for(;;)
        timer_spin_sleep(100);
#endif
}

void dc_boot_halt(uint16_t rgb565)
{
    // Take the display back from the PVR, then never return.
    pvr_shutdown();
    vid_set_mode(DM_640x480, PM_RGB565);

    for(;;)
    {
        for(int i = 0; i < 640 * 480; ++i)
            vram_s[i] = rgb565;
        timer_spin_sleep(100);
    }
}

// Priority 101 is the earliest a user constructor can run: the linker sorts it
// to the tail of .ctors, which __do_global_ctors_aux walks backwards.
__attribute__((constructor(101)))
static void dc_boot_probe_first_ctor()
{
    dc_boot_probe(DC_PROBE_BLUE);
}

#endif // THEXTECH_DC_BOOT_PROBE
