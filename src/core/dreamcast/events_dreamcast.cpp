/*
 * TheXTech Dreamcast backend
 */

#include <kos.h>

#ifdef ERR_OK
#   undef ERR_OK
#endif

#include "core/events.h"
#include "globals.h"
#include "sdl_proxy/mixer.h"

namespace XEvents
{

bool init()
{
    return true;
}

void quit()
{
}

void doEvents()
{
    Mix_DC_PumpMusic();

    maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if(!cont)
        return;

    cont_state_t *state = (cont_state_t *)maple_dev_status(cont);
    if(!state)
        return;

    // Soft quit: Start + A + B held together
    if((state->buttons & (CONT_START | CONT_A | CONT_B)) == (CONT_START | CONT_A | CONT_B))
        GameIsActive = false;
}

void waitEvents()
{
    thd_sleep(1);
}

void eventResize() {}

} // namespace XEvents
