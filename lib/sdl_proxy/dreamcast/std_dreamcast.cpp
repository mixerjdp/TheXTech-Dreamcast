/*
 * TheXTech Dreamcast — SDL timer proxy via KallistiOS
 */

#include "../sdl_timer.h"
#include <kos.h>

#ifdef ERR_OK
#   undef ERR_OK
#endif

uint32_t SDL_GetTicks()
{
    return timer_ms_gettime64();
}

uint64_t SDL_GetMicroTicks()
{
    return static_cast<uint64_t>(timer_ms_gettime64()) * 1000ull;
}

void SDL_Delay(int ms)
{
    thd_sleep(ms > 0 ? ms : 0);
}
