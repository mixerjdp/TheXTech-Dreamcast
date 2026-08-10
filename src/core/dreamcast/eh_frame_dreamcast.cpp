/*
 * TheXTech Dreamcast — replacements for C++ runtime pieces that fault here.
 *
 * WHY THIS EXISTS
 *
 * On this KallistiOS / DreamSDK toolchain two pieces of the GCC C++ runtime
 * fault on the Dreamcast. Both reach KOS through the "kos" gthread layer that
 * libgcc and libstdc++ were built against (bits/gthr-default.h, which maps onto
 * KOS mutexes and TLS), which is the common thread between them.
 *
 * 1. __register_frame_info() (libgcc, unwind-dw2-fde.c)
 *
 *    crtbegin's frame_dummy calls it from _init(), before the first static
 *    constructor, and it faults. Every binary linking any libstdc++ object —
 *    even one as small as `operator new` — died at startup with
 *    "Fatal: SH4 exception when blocked" and never reached main(). KallistiOS's
 *    own cpp/concurrency example fails identically, under a real Dreamcast BIOS
 *    as well as Flycast's REIOS, so this is the toolchain and not TheXTech.
 *
 *    Calling it later does not help: deferring the registration to the top of
 *    main(), once the whole kernel is up, faults just the same. It is the call
 *    itself that is broken, not its timing.
 *
 *    So it is stubbed out. THE COST IS REAL: the unwinder gets no frame tables,
 *    so a thrown C++ exception cannot find its handler and terminates the
 *    program instead of unwinding. PGE File Formats throws while reading world
 *    and level files, which is why picking an episode from the menu still
 *    crashes. Fixing that needs the toolchain rebuilt so libgcc and libstdc++
 *    match the installed KallistiOS — see platforms/dreamcast/HANDOFF.md.
 *
 * 2. __cxa_guard_acquire/release/abort (libstdc++, libsupc++/guard.cc)
 *
 *    These serialise initialisation of function-local statics through the same
 *    gthread layer, and faulted on the first one the engine constructs
 *    (g_config_backup in UpdateConfig). Startup is single-threaded, so a plain
 *    "has it run yet" byte is all the guard needs to be.
 *
 * The frame functions are replaced through -Wl,--wrap; the guards through
 * -Wl,--allow-multiple-definition.
 */

#include <cstdint>

#ifdef THEXTECH_DC_EH_BISECT
#   include <kos.h>
#   ifdef ERR_OK
#       undef ERR_OK
#   endif
#   include "core/dreamcast/boot_probe_dreamcast.h"

static void s_once_dummy() {}
#endif

extern "C" {

#ifdef THEXTECH_DC_EH_BISECT
void __real___register_frame_info(const void *begin, void *ob);
#endif

// --- DWARF exception-frame registry ---------------------------------------
// No-ops. See the note above: exceptions cannot unwind while these are stubs.

void __wrap___register_frame_info(const void *begin, void *ob)
{
#ifdef THEXTECH_DC_EH_BISECT
    // Runs from _init(), before the PVR is up, so plain framebuffer writes
    // survive on screen: the last colour shown is the step that faulted.
    dc_boot_probe(0x001Fu);                    // 1 azul   - entramos
    if(!begin)
    {
        dc_boot_probe(0xF800u);                // rojo     - begin nulo
        return;
    }

    volatile uint32_t first = *static_cast<const uint32_t *>(begin);
    (void)first;
    dc_boot_probe(0x07E0u);                    // 2 verde  - .eh_frame legible

    static kthread_once_t s_once = KTHREAD_ONCE_INIT;
    kthread_once(&s_once, s_once_dummy);
    dc_boot_probe(0xFFE0u);                    // 3 amar.  - kthread_once OK

    static mutex_t s_mtx = MUTEX_INITIALIZER;
    mutex_lock(&s_mtx);
    mutex_unlock(&s_mtx);
    dc_boot_probe(0xF81Fu);                    // 4 magen. - mutex OK

    __real___register_frame_info(begin, ob);
    dc_boot_probe(0x07FFu);                    // 5 cian   - la real sobrevive
    return;
#endif

    (void)begin;
    (void)ob;
}

void *__wrap___deregister_frame_info(const void *begin)
{
    (void)begin;
    return nullptr;
}

// --- function-local static guards -----------------------------------------
//
// The Itanium ABI guard is 64 bits wide; only the first byte carries the
// "initialised" flag on little-endian targets such as SH4.

int __cxa_guard_acquire(uint64_t *guard)
{
    return *reinterpret_cast<uint8_t *>(guard) == 0;
}

void __cxa_guard_release(uint64_t *guard)
{
    *reinterpret_cast<uint8_t *>(guard) = 1;
}

void __cxa_guard_abort(uint64_t *guard)
{
    (void)guard;
}

} // extern "C"
