/*
 * TheXTech Dreamcast backend
 */

#include "core/power.h"

namespace XPower
{

StatusInfo devicePowerStatus()
{
    StatusInfo res;
    res.power_status = StatusInfo::POWER_UNKNOWN;
    res.power_level = 1.0_nf;
    return res;
}

} // namespace XPower
