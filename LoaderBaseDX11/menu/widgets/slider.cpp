#include "../headers/functions.h"
#include "../headers/widgets.h"

//
// XISFPS slider — ported 1:1 from the reference project's `SliderHash` (see
// LoaderBaseDX11/includes/custom/custom.hpp:244).
//
// Visual anatomy:
//   ┌──────────────────────────────────────┐  ← label top-left, value top-right
//   │ Horizontal Smoothing            10  │      both live 6 px ABOVE the frame
//   │ ┌────────────╢───────────────────┐  │  ← rounded frame (h=17, r=4)
//   │ │████████████║                    │  │      fill: left-anchored, grey
//   │ └────────────╢───────────────────┘  │      grab: vertical bar 4 px wide, light-grey
//   └──────────────────────────────────────┘
//
// Palette (matches the other project literals):
//   frame bg   = clr->widgets.background    (22, 22, 23)
//   fill       = clr->widgets.text_inactive (80, 80, 80)  — via accent when the
//                                                           user recolors to grey
//   grab       = (200, 200, 200)             — always light so it reads
//   text dim   = (0.40, 0.40, 0.40) when value == 0
//   text bright= (0.80, 0.80, 0.80) when value  > 0
//   rounding   = 4 px everywhere
//

template <typename T>
widget_t slider_ex(std::string_view name, T* callback, T min, T max, std::string_view format, float width = 0.f)
{
    struct anim_t
    {
        float grab_pos{ 0 };     // eased fill/grab X position (px, within track)
        float text_bright{ 0 };  // 0..1: dim→bright text as value moves off zero
    };

    ImFont* fnt = font->get(onest_medium_data, 14);
    const float label_h = xgui->text_size(fnt, name.data()).y;
    const float text_gap = SCALE(6.f);
    const float frame_h  = SCALE(17.f);
    const float rounding = SCALE(4.f);
    const float grab_w   = SCALE(4.f);

    // Total row height reserves label + 6 gap + frame.
    const float row_h = label_h + text_gap + frame_h;

    const float final_w = width > 0.f ? width : xgui->content_avail().x;
    widget_t data = xgui->register_item(name, ImVec2(final_w, row_h));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);

    // Frame lives at the BOTTOM of the row; label + value paint above it.
    const ImRect frame{
        ImVec2(data->rect.Min.x, data->rect.Max.y - frame_h),
        data->rect.Max
    };

    bool hovered, held;
    bool pressed = xgui->button_behavior(frame, data->id, &hovered, &held);
    data->value_changed = held;

    if (held)
    {
        const float t = ImSaturate((xgui->mouse_pos().x - frame.Min.x) / frame.GetWidth());
        T new_v = static_cast<T>(min + t * (max - min));
        *callback = ImClamp(new_v, min, max);
    }

    // Eased position of the grab / fill.
    const float pct  = (max != min) ? float(*callback - min) / float(max - min) : 0.f;
    const float target_x = pct * frame.GetWidth();
    xgui->easing(anim->grab_pos, target_x, 20.f, dynamic_easing);

    // Value-is-zero dims label + value; anything else brightens them.
    const bool is_zero = (*callback == static_cast<T>(0));
    xgui->easing(anim->text_bright, is_zero ? 0.f : 1.f, 10.f, static_easing);

    // ---- Frame ----
    draw->rect_filled(xgui->window_drawlist(), frame.Min, frame.Max,
        draw->get_clr(clr->widgets.background), rounding);

    // ---- Fill (left-anchored) ----
    if (anim->grab_pos > 0.5f)
    {
        draw->rect_filled(xgui->window_drawlist(),
            ImVec2(frame.Min.x + 1, frame.Min.y),
            ImVec2(frame.Min.x + anim->grab_pos + 1, frame.Max.y),
            draw->get_clr(clr->accent),
            rounding, draw_flags_round_corners_left);
    }

    // ---- Border ----
    draw->rect(xgui->window_drawlist(), frame.Min, frame.Max,
        draw->get_clr(clr->widgets.stroke), rounding, 0, SCALE(1));

    // ---- Grab (vertical bar) ----
    const ImVec2 grab_min{ frame.Min.x + anim->grab_pos - grab_w * 0.5f, frame.Min.y };
    const ImVec2 grab_max{ frame.Min.x + anim->grab_pos + grab_w * 0.5f, frame.Max.y };
    // Fixed light-grey grab, matches literal (200,200,200) from the source project.
    const ImU32 grab_col = draw->get_clr(ImVec4(200.f/255.f, 200.f/255.f, 200.f/255.f, 1.f));
    draw->rect_filled(xgui->window_drawlist(), grab_min, grab_max, grab_col, rounding);

    // ---- Label (top-left) and value (top-right) ----
    ImVec4 dim   { 0.40f, 0.40f, 0.40f, 1.f };
    ImVec4 bright{ 0.80f, 0.80f, 0.80f, 1.f };
    ImVec4 text_col = ImLerp(dim, bright, anim->text_bright);

    char value_buf[64];
    std::snprintf(value_buf, sizeof value_buf, format.data(), *callback);

    // Top of the row is where the labels sit — text_clipped aligns them.
    draw->text_clipped(xgui->window_drawlist(), fnt,
        data->rect.Min, ImVec2(data->rect.Max.x, frame.Min.y - text_gap),
        draw->get_clr(text_col), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));
    draw->text_clipped(xgui->window_drawlist(), fnt,
        data->rect.Min, ImVec2(data->rect.Max.x, frame.Min.y - text_gap),
        draw->get_clr(text_col), value_buf, NULL, NULL, ImVec2(1.f, 0.f));

    if (var->gui.section_alpha == 0.f || var->gui.sub_section_alpha == 0.f)
    {
        anim->grab_pos = 0.f;
    }

    return data;
}

widget_t c_widgets::slider_int(std::string_view key)
{
    slider_int_t* data = cfg->fill<slider_int_t>(key.data());
    return slider_ex(data->name, &data->callback, data->min, data->max, data->format);
}

widget_t c_widgets::slider_float(std::string_view key)
{
    slider_float_t* data = cfg->fill<slider_float_t>(key.data());
    return slider_ex(data->name, &data->callback, data->min, data->max, data->format);
}

widget_t c_widgets::slider_float_raw(std::string_view name, float* value, float min, float max, std::string_view format, float width)
{
    return slider_ex<float>(name, value, min, max, format, width);
}
