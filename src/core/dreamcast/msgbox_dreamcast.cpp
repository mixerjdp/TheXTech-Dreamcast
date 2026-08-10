/*
 * TheXTech Dreamcast backend
 */

#include <kos.h>
#include <cstdio>

#ifdef ERR_OK
#   undef ERR_OK
#endif

#include "core/msgbox.h"
#include "globals.h"

namespace XMsgBox
{

bool init()
{
    return true;
}

void quit() {}

int simpleMsgBox(uint32_t flags, const std::string &title, const std::string &message)
{
    UNUSED(flags);
    printf("\n==== %s ====\n%s\n(Press START)\n", title.c_str(), message.c_str());

    while(GameIsActive)
    {
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if(cont)
        {
            cont_state_t *state = (cont_state_t *)maple_dev_status(cont);
            if(state && (state->buttons & CONT_START))
                break;
        }
        thd_sleep(16);
    }

    return 0;
}

void errorMsgBox(const std::string &title, const std::string &message)
{
    simpleMsgBox(MESSAGEBOX_ERROR, title, message);
}

} // namespace XMsgBox
