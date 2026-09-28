#include "../headers/functions.h"
#include "../headers/widgets.h"

#include "../headers/functions.h"
#include "../headers/widgets.h"

const char* const keys[] = { "Tab", "Left", "Right", "Up", "Down", "Page Up", "Page Down", "Home", "End", "Insert", "Delete", "Backspace", "Space", "Enter", "Escape", "Ctrl", "Shift", "Alt", "Super", "Ctrl", "Shift", "Alt", "Super", "Menu", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "Apostrophe", "Comma", "Minus", "Period", "Slash", "Semicolon", "Equal", "Left Bracket", "Backslash", "Right Bracket", "Grave Accent", "Caps Lock", "Scroll Lock", "Num Lock", "Print Screen", "Pause", "Keypad 0", "Keypad 1", "Keypad 2", "Keypad 3", "Keypad 4", "Keypad 5", "Keypad 6", "Keypad 7", "Keypad 8", "Keypad 9", "Keypad .", "Keypad /", "Keypad *", "Keypad -", "Keypad +", "Keypad Enter", "Keypad =", "App Back", "App Forward", "Gamepad Start", "Gamepad Back", "Gamepad Face Left", "Gamepad Face Right", "Gamepad Face Up", "Gamepad Face Down", "Gamepad Dpad Left", "Gamepad Dpad Right", "Gamepad Dpad Up", "Gamepad Dpad Down", "Gamepad L1", "Gamepad R1", "Gamepad L2", "Gamepad R2", "Gamepad L3", "Gamepad R3", "Gamepad L Stick Left", "Gamepad L Stick Right", "Gamepad L Stick Up", "Gamepad L Stick Down", "Gamepad R Stick Left", "Gamepad R Stick Right", "Gamepad R Stick Up", "Gamepad R Stick Down", "Mouse 1", "Mouse 2", "Mouse 3", "Mouse 4", "Mouse 5", "Mouse Wheel X", "Mouse Wheel Y", "Ctrl", "Shift", "Alt", "Super" };

const char* get_key_name(ImGuiKey key)
{
    if (key == ImGuiKey_None)
        return "...";

    constexpr int names_count = (int)(sizeof(keys) / sizeof(keys[0]));
    const int idx = (int)key - (int)ImGuiKey_NamedKey_BEGIN;
    if (idx < 0 || idx >= names_count)
        return "...";

    return keys[idx];
}

widget_t c_widgets::key_select(std::string_view name, int* key)
{
    struct anim_t
    {
        bool active{ false };
        bool armed{ false };
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text };
    };

    widget_t data = xgui->register_item(name, ImVec2(xgui->content_avail().x, SCALE(elements->keybind.height)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);
    std::string buf_display = anim->active ? "..." : get_key_name((ImGuiKey)*key);

    if (data->hovered && xgui->mouse_released(mouse_button_left) && xgui->is_window_hovered())
    {
        anim->active = true;
        anim->armed  = false;
    }

    if (anim->active)
    {
        if (!anim->armed)
        {
            bool any_down = false;
            constexpr int names_count = (int)(sizeof(keys) / sizeof(keys[0]));
            const int lo = (int)ImGuiKey_NamedKey_BEGIN;
            const int hi = lo + names_count;
            for (int i = lo; i < hi && i < ImGuiKey_COUNT; ++i)
            {
                if (IsKeyDown((ImGuiKey)i)) { any_down = true; break; }
            }
            if (!any_down) anim->armed = true;
        }
        else if (IsKeyPressed(ImGuiKey_Escape))
        {
            *key = 0;
            anim->active = false;
            anim->armed  = false;
        }
        else
        {
            constexpr int names_count = (int)(sizeof(keys) / sizeof(keys[0]));
            const int lo = (int)ImGuiKey_NamedKey_BEGIN;
            const int hi = lo + names_count;
            for (int i = lo; i < hi && i < ImGuiKey_COUNT; ++i)
            {
                if (i == ImGuiKey_Escape) continue;
                if (IsKeyPressed((ImGuiKey)i))
                {
                    *key = i;
                    anim->active = false;
                    anim->armed  = false;
                    break;
                }
            }
        }
    }

    xgui->easing(anim->alpha, anim->active ? 1.f : 0.f, 8.f, static_easing);

    // XISFPS key capsule:
    //  - Border tracks the arming state so an armed input pulses toward accent.
    //  - Fill gets a stronger magenta wash while listening for a key.
    ImVec4 stroke_col = ImLerp(clr->widgets.stroke.Value,
                               ImVec4(clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, 0.7f),
                               anim->alpha);

    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->keybind.rounding));
    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->accent, 0.28f * anim->alpha), SCALE(elements->keybind.rounding));
    draw->rect(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(stroke_col), SCALE(elements->keybind.rounding), 0, SCALE(1));

    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.text), buf_display.data(), NULL, NULL, ImVec2(0.5f, 0.5f));

    return data;
}

widget_t c_widgets::mode_select(std::string_view name, int* mode)
{
    struct anim_t
    {
        float alpha[2]{ 0, 0 };
        ImVec4 text[2]{ clr->widgets.text, clr->widgets.text };
    };

    widget_t data = xgui->register_item(name, ImVec2(xgui->content_avail().x, SCALE(elements->keybind.height)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);
    const ImRect toggle{ data->rect.Min, ImVec2(data->rect.GetCenter().x - var->style.item_spacing.x / 2, data->rect.Max.y) };
    const ImRect hold{ ImVec2(data->rect.GetCenter().x + var->style.item_spacing.x / 2, data->rect.Min.y), data->rect.Max };

    if (toggle.Contains(xgui->mouse_pos()) && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered())
        *mode = 0;
    if (hold.Contains(xgui->mouse_pos()) && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered())
        *mode = 1;

    xgui->easing(anim->alpha[0], *mode == 0 ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->alpha[1], *mode == 1 ? 1.f : 0.f, 8.f, static_easing);

    xgui->easing(anim->text[0], *mode == 0 ? clr->widgets.background.Value : clr->widgets.text.Value, 14.f, dynamic_easing);
    xgui->easing(anim->text[1], *mode == 1 ? clr->widgets.background.Value : clr->widgets.text.Value, 14.f, dynamic_easing);

    // XISFPS Toggle/Hold selector — each pill gets its own border that shifts
    // toward the accent when active, so the split reads even in monochrome.
    const float pill_rounding = SCALE(elements->keybind.rounding);

    auto draw_pill = [&](const ImRect& r, float alpha, const ImVec4& text_clr, const char* label)
    {
        ImVec4 stroke_col = ImLerp(clr->widgets.stroke.Value,
                                   ImVec4(clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, 0.8f),
                                   alpha);
        draw->rect_filled(xgui->window_drawlist(), r.Min, r.Max, draw->get_clr(clr->widgets.background), pill_rounding);
        draw->rect_filled(xgui->window_drawlist(), r.Min, r.Max, draw->get_clr(clr->accent, alpha * 0.85f), pill_rounding);
        draw->rect(xgui->window_drawlist(), r.Min, r.Max, draw->get_clr(stroke_col), pill_rounding, 0, SCALE(1));
        draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), r.Min, r.Max, draw->get_clr(text_clr), label, NULL, NULL, ImVec2(0.5f, 0.5f));
    };

    draw_pill(toggle, anim->alpha[0], anim->text[0], "Toggle");
    draw_pill(hold,   anim->alpha[1], anim->text[1], "Hold");

    return data;
}