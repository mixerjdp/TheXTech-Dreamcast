/*
 * TheXTech Dreamcast backend — Maple controller input
 */

#ifndef INPUT_DREAMCAST_H
#define INPUT_DREAMCAST_H

#include "../controls.h"

namespace Controls
{

constexpr int dc_null_key = -1;

enum DCPadButton : int
{
    DC_BTN_A = 0,
    DC_BTN_B,
    DC_BTN_X,
    DC_BTN_Y,
    DC_BTN_START,
    DC_BTN_LEFT,
    DC_BTN_RIGHT,
    DC_BTN_UP,
    DC_BTN_DOWN,
    DC_BTN_L,
    DC_BTN_R,
    DC_BTN_COUNT
};

class InputMethod_Dreamcast : public InputMethod
{
public:
    using InputMethod::Type;
    using InputMethod::Profile;

    int m_port = 0;

    ~InputMethod_Dreamcast();

    bool Update(int player, Controls_t &c, CursorControls_t &m, EditorControls_t &e, HotkeysPressed_t &h);
    void Rumble(int ms, float strength);
    StatusInfo GetStatus();
};

class InputMethodProfile_Dreamcast : public InputMethodProfile
{
public:
    using InputMethodProfile::Name;
    using InputMethodProfile::Type;

    int m_keys[PlayerControls::n_buttons] = {dc_null_key};
    int m_keys2[PlayerControls::n_buttons] = {dc_null_key};
    int m_editor_keys[EditorControls::n_buttons] = {dc_null_key};
    int m_editor_keys2[EditorControls::n_buttons] = {dc_null_key};
    int m_cursor_keys2[CursorControls::n_buttons] = {dc_null_key};
    int m_hotkeys[Hotkeys::n_buttons] = {dc_null_key};
    int m_hotkeys2[Hotkeys::n_buttons] = {dc_null_key};

    InputMethodProfile_Dreamcast();

    bool PollPrimaryButton(ControlsClass c, size_t i);
    bool PollSecondaryButton(ControlsClass c, size_t i);
    bool DeletePrimaryButton(ControlsClass c, size_t i);
    bool DeleteSecondaryButton(ControlsClass c, size_t i);
    const char *NamePrimaryButton(ControlsClass c, size_t i);
    const char *NameSecondaryButton(ControlsClass c, size_t i);
    void SaveConfig(IniProcessing *ctl);
    void LoadConfig(IniProcessing *ctl);
};

class InputMethodType_Dreamcast : public InputMethodType
{
private:
    InputMethodProfile *AllocateProfile() noexcept;

public:
    using InputMethodType::Name;
    using InputMethodType::m_profiles;

    int m_numPlayers = 0;

    InputMethodType_Dreamcast();
    ~InputMethodType_Dreamcast();

    bool TestProfileType(InputMethodProfile *profile);
    bool RumbleSupported();
    void UpdateControlsPre();
    void UpdateControlsPost();

    InputMethod *Poll(const std::vector<InputMethod *> &active_methods) noexcept;
};

} // namespace Controls

#endif
