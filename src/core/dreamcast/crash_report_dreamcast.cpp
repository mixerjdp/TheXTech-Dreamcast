/*
 * TheXTech Dreamcast — on-screen crash reporter.
 *
 * There is no console to read back from Flycast, so a fault just freezes with
 * "Fatal: SH4 exception when blocked" and no clue where. This installs a global
 * SH4 exception handler that paints the faulting PC (and the exception code) on
 * screen as binary bars, which a screenshot can then decode:
 *
 *     bit set   -> white bar
 *     bit clear -> dark bar
 *
 * Row 1 is the PC, MSB on the left. Feed it back through
 *     sh-elf-addr2line -f -C -e build-dc/output/bin/thextech.elf <pc>
 * to get the exact function and line.
 *
 * Row 2 is the exception code (see arch/irq.h EXC_*).
 *
 * Debug aid only: built when THEXTECH_DC_BOOT_PROBE is on.
 */

#include "core/dreamcast/crash_report_dreamcast.h"

#ifdef THEXTECH_DC_BOOT_PROBE

#include <kos.h>
#include <dc/video.h>
#include <arch/irq.h>

#ifdef ERR_OK
#   undef ERR_OK
#endif

static void s_fill(int x0, int y0, int w, int h, uint16_t colour)
{
    for(int y = y0; y < y0 + h; ++y)
    {
        if(y < 0 || y >= 480)
            continue;

        for(int x = x0; x < x0 + w; ++x)
        {
            if(x < 0 || x >= 640)
                continue;

            vram_s[y * 640 + x] = colour;
        }
    }
}

//! Draws a 32-bit value as bars, most significant bit on the left.
static void s_draw_word(uint32_t value, int y0, int height)
{
    const int bar_w = 640 / 32; // 20px per bit

    for(int bit = 0; bit < 32; ++bit)
    {
        bool set = (value >> (31 - bit)) & 1u;
        s_fill(bit * bar_w, y0, bar_w - 2, height, set ? 0xFFFFu : 0x18C3u);
    }
}

static void s_crash_handler(irq_t code, irq_context_t *context, void *data)
{
    (void)data;

    pvr_shutdown();
    vid_set_mode(DM_640x480, PM_RGB565);

    for(;;)
    {
        // Red background so a crash is unmistakable.
        s_fill(0, 0, 640, 480, 0xF800u);

        s_draw_word(context ? CONTEXT_PC(*context) : 0u, 120, 90);
        s_draw_word(static_cast<uint32_t>(code), 260, 90);

        timer_spin_sleep(100);
    }
}

void dc_install_crash_handler()
{
    // Only real faults. A global handler would also swallow ordinary IRQs
    // (EXC_IRQD and friends), which are not crashes at all.
    static const irq_t s_faults[] =
    {
        EXC_ILLEGAL_INSTR,
        EXC_SLOT_ILLEGAL_INSTR,
        EXC_INSTR_ADDRESS,       // == EXC_DATA_ADDRESS_READ
        EXC_DATA_ADDRESS_WRITE,
        EXC_ITLB_MISS,           // == EXC_DTLB_MISS_READ
        EXC_DTLB_MISS_WRITE,
        EXC_ITLB_PV,             // == EXC_DTLB_PV_READ
        EXC_DTLB_PV_WRITE,
        EXC_ITLB_MULTIPLE,
        EXC_GENERAL_FPU,
        EXC_SLOT_FPU,
    };

    for(irq_t code : s_faults)
        arch_irq_set_handler(code, s_crash_handler, nullptr);
}

#endif // THEXTECH_DC_BOOT_PROBE
