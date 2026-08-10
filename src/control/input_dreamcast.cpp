/*
 * TheXTech Dreamcast backend — Maple controller input
 */

#include <kos.h>

#ifdef ERR_OK
#   undef ERR_OK
#endif

#include "globals.h"
#include "controls.h"
#include "control/input_dreamcast.h"
#include "control/controls_strings.h"
#include "main/menu_main.h"

namespace Controls
{

static const char *s_btn_name(int id)
{
    switch(id)
    {
    case DC_BTN_A: return "A";
    case DC_BTN_B: return "B";
    case DC_BTN_X: return "X";
    case DC_BTN_Y: return "Y";
    case DC_BTN_START: return "Start";
    case DC_BTN_LEFT: return "Left";
    case DC_BTN_RIGHT: return "Right";
    case DC_BTN_UP: return "Up";
    case DC_BTN_DOWN: return "Down";
    case DC_BTN_L: return "L";
    case DC_BTN_R: return "R";
    default: return g_mainMenu.caseNone.c_str();
    }
}

static uint32_t s_read_buttons(int port)
{
    maple_device_t *dev = maple_enum_type(port, MAPLE_FUNC_CONTROLLER);
    if(!dev)
        return 0;
    cont_state_t *st = (cont_state_t *)maple_dev_status(dev);
    if(!st)
        return 0;

    uint32_t bits = 0;
    if(st->buttons & CONT_A) bits |= (1u << DC_BTN_A);
    if(st->buttons & CONT_B) bits |= (1u << DC_BTN_B);
    if(st->buttons & CONT_X) bits |= (1u << DC_BTN_X);
    if(st->buttons & CONT_Y) bits |= (1u << DC_BTN_Y);
    if(st->buttons & CONT_START) bits |= (1u << DC_BTN_START);
    if(st->buttons & CONT_DPAD_LEFT) bits |= (1u << DC_BTN_LEFT);
    if(st->buttons & CONT_DPAD_RIGHT) bits |= (1u << DC_BTN_RIGHT);
    if(st->buttons & CONT_DPAD_UP) bits |= (1u << DC_BTN_UP);
    if(st->buttons & CONT_DPAD_DOWN) bits |= (1u << DC_BTN_DOWN);
    if(st->ltrig > 64) bits |= (1u << DC_BTN_L);
    if(st->rtrig > 64) bits |= (1u << DC_BTN_R);

    // Analog as d-pad
    if(st->joyx < -64) bits |= (1u << DC_BTN_LEFT);
    if(st->joyx > 64) bits |= (1u << DC_BTN_RIGHT);
    if(st->joyy < -64) bits |= (1u << DC_BTN_UP);
    if(st->joyy > 64) bits |= (1u << DC_BTN_DOWN);
    return bits;
}

static bool s_held(uint32_t bits, int key)
{
    return key >= 0 && key < DC_BTN_COUNT && (bits & (1u << key)) != 0;
}

InputMethod_Dreamcast::~InputMethod_Dreamcast()
{
    auto *t = dynamic_cast<InputMethodType_Dreamcast *>(Type);
    if(t)
        t->m_numPlayers--;
}

bool InputMethod_Dreamcast::Update(int player, Controls_t &c, CursorControls_t &m, EditorControls_t &e, HotkeysPressed_t &h)
{
    (void)player;
    (void)m;
    (void)e;
    (void)h;

    auto *p = dynamic_cast<InputMethodProfile_Dreamcast *>(Profile);
    if(!p)
        return false;

    uint32_t bits = s_read_buttons(m_port);
    if(m_port == 0 && bits == 0 && !maple_enum_type(0, MAPLE_FUNC_CONTROLLER))
        return true; // keep method alive even if pad briefly missing

    for(size_t i = 0; i < PlayerControls::n_buttons; i++)
    {
        bool down = s_held(bits, p->m_keys[i]) || s_held(bits, p->m_keys2[i]);
        switch(i)
        {
        case PlayerControls::Buttons::Up: c.Up = down; break;
        case PlayerControls::Buttons::Down: c.Down = down; break;
        case PlayerControls::Buttons::Left: c.Left = down; break;
        case PlayerControls::Buttons::Right: c.Right = down; break;
        case PlayerControls::Buttons::Jump: c.Jump = down; break;
        case PlayerControls::Buttons::AltJump: c.AltJump = down; break;
        case PlayerControls::Buttons::Run: c.Run = down; break;
        case PlayerControls::Buttons::AltRun: c.AltRun = down; break;
        case PlayerControls::Buttons::Drop: c.Drop = down; break;
        case PlayerControls::Buttons::Start: c.Start = down; break;
        default: break;
        }
    }

    return true;
}

void InputMethod_Dreamcast::Rumble(int, float) {}

StatusInfo InputMethod_Dreamcast::GetStatus()
{
    StatusInfo s;
    s.power_status = StatusInfo::POWER_UNKNOWN;
    return s;
}

InputMethodProfile_Dreamcast::InputMethodProfile_Dreamcast()
{
    Name = "Dreamcast Pad";

    // Brace initialisation only sets the first element; the rest would default
    // to 0, which is DC_BTN_A, and every control would fire on the A button.
    for(size_t i = 0; i < PlayerControls::n_buttons; i++)
        m_keys[i] = m_keys2[i] = dc_null_key;
    for(size_t i = 0; i < EditorControls::n_buttons; i++)
        m_editor_keys[i] = m_editor_keys2[i] = dc_null_key;
    for(size_t i = 0; i < CursorControls::n_buttons; i++)
        m_cursor_keys2[i] = dc_null_key;
    for(size_t i = 0; i < Hotkeys::n_buttons; i++)
        m_hotkeys[i] = m_hotkeys2[i] = dc_null_key;

    // Defaults: SMBX-like layout on DC pad
    m_keys[PlayerControls::Buttons::Jump] = DC_BTN_A;
    m_keys[PlayerControls::Buttons::Run] = DC_BTN_X;
    m_keys[PlayerControls::Buttons::AltJump] = DC_BTN_B;
    m_keys[PlayerControls::Buttons::AltRun] = DC_BTN_Y;
    m_keys[PlayerControls::Buttons::Drop] = DC_BTN_L;
    m_keys[PlayerControls::Buttons::Start] = DC_BTN_START;
    m_keys[PlayerControls::Buttons::Up] = DC_BTN_UP;
    m_keys[PlayerControls::Buttons::Down] = DC_BTN_DOWN;
    m_keys[PlayerControls::Buttons::Left] = DC_BTN_LEFT;
    m_keys[PlayerControls::Buttons::Right] = DC_BTN_RIGHT;

    // The right trigger doubles as run, which is what most DC platformers do.
    m_keys2[PlayerControls::Buttons::Run] = DC_BTN_R;
}

bool InputMethodProfile_Dreamcast::PollPrimaryButton(ControlsClass, size_t) { return false; }
bool InputMethodProfile_Dreamcast::PollSecondaryButton(ControlsClass, size_t) { return false; }
bool InputMethodProfile_Dreamcast::DeletePrimaryButton(ControlsClass, size_t) { return false; }
bool InputMethodProfile_Dreamcast::DeleteSecondaryButton(ControlsClass, size_t) { return false; }

const char *InputMethodProfile_Dreamcast::NamePrimaryButton(ControlsClass c, size_t i)
{
    int k = dc_null_key;
    if(c == ControlsClass::Player && i < PlayerControls::n_buttons)
        k = m_keys[i];
    return s_btn_name(k);
}

const char *InputMethodProfile_Dreamcast::NameSecondaryButton(ControlsClass c, size_t i)
{
    int k = dc_null_key;
    if(c == ControlsClass::Player && i < PlayerControls::n_buttons)
        k = m_keys2[i];
    return s_btn_name(k);
}

void InputMethodProfile_Dreamcast::SaveConfig(IniProcessing *) {}
void InputMethodProfile_Dreamcast::LoadConfig(IniProcessing *) {}

InputMethodProfile *InputMethodType_Dreamcast::AllocateProfile() noexcept
{
    return new(std::nothrow) InputMethodProfile_Dreamcast;
}

InputMethodType_Dreamcast::InputMethodType_Dreamcast()
{
    Name = "Dreamcast Controller";
    m_profiles.push_back(AllocateProfile());
}

InputMethodType_Dreamcast::~InputMethodType_Dreamcast() {}

bool InputMethodType_Dreamcast::TestProfileType(InputMethodProfile *profile)
{
    return dynamic_cast<InputMethodProfile_Dreamcast *>(profile) != nullptr;
}

bool InputMethodType_Dreamcast::RumbleSupported() { return false; }
void InputMethodType_Dreamcast::UpdateControlsPre() {}
void InputMethodType_Dreamcast::UpdateControlsPost() {}

InputMethod *InputMethodType_Dreamcast::Poll(const std::vector<InputMethod *> &active_methods) noexcept
{
    // One player per Maple port.
    if(m_numPlayers >= 4)
        return nullptr;

    for(int port = 0; port < 4; ++port)
    {
        if(!maple_enum_type(port, MAPLE_FUNC_CONTROLLER))
            continue;

        bool used = false;
        for(InputMethod *m : active_methods)
        {
            auto *dc = dynamic_cast<InputMethod_Dreamcast *>(m);
            if(dc && dc->m_port == port)
            {
                used = true;
                break;
            }
        }
        if(used)
            continue;

        if(s_read_buttons(port) == 0)
            continue;

        auto *method = new(std::nothrow) InputMethod_Dreamcast;
        if(!method)
            return nullptr;

        method->Type = this;
        method->Profile = m_profiles.empty() ? AllocateProfile() : m_profiles[0];
        method->Name = "DC Pad " + std::to_string(port + 1);
        method->m_port = port;
        m_numPlayers++;
        return method;
    }

    return nullptr;
}

} // namespace Controls
