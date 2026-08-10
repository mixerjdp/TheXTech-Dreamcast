/*
 * TheXTech Dreamcast — KallistiOS startup configuration.
 *
 * Without an explicit KOS_INIT_FLAGS, KOS decides what to initialise from weak
 * symbols: a subsystem is only brought up if something in the binary already
 * references it. The engine never names fs_ramdisk_init directly, so "/ram"
 * — the only writable location we have for settings and saves — would never be
 * mounted. Declaring the flags explicitly pulls it in.
 *
 * INIT_DEFAULT covers IRQs, the filesystems (romdisk, ramdisk, pty, /dev/null,
 * /dev/urandom), the Maple peripherals and the GD-ROM.
 */

#include <kos.h>

#ifdef ERR_OK
#   undef ERR_OK
#endif

KOS_INIT_FLAGS(INIT_DEFAULT);
