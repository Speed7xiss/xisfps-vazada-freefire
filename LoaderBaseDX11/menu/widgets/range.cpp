#include "../headers/functions.h"
#include "../headers/widgets.h"

template <typename T>
widget_t range_ex(std::string_view name, T* callback, T* callback_two, T min, T max, T range, std::string_view format)
{
    struct anim_t
    {
        float offset[2]{ 0, 0 };
        bool grab_active[2]{ false, false };
        T slow_value[2]{ 0, 0 };
    };

    widget_t data = xgui->register_item(name, ImVec2(xgui->content_avail().x, SCALE(elements->slider.height)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);
    const ImRect rect{ ImVec2(data->rect.Min.x, data->rect.Max.y - SCALE(elements->slider.size)), data->rect.Max };

    bool hovered, held;
    bool pressed = xgui->button_behavior(rect, data->id, &hovered, &held);

    const float padding = SCALE(0);
    const float grab_width = SCALE(elements->slider.size);
    const float width_with_grab = rect.GetWidth() - grab_width - padding * 2;

    const float grab_offset = padding + grab_width / 2 + static_cast<float>((*callback - min)) / static_cast<float>(max - min) * width_with_grab;
    float grab_offset2 = padding + grab_width / 2 + static_cast<float>((*callback_two - min)) / static_cast<float>(max - min) * width_with_grab;

    char value_buf1[64]; const char* value_buf_end1 = xgui->get_fmt(value_buf1, callback, format);
    char value_buf2[64]; const char* value_buf_end2 = xgui->get_fmt(value_buf2, callback_two, format);
    const float t = ImSaturate((xgui->mouse_pos().x - (rect.Min.x + padding + grab_width / 2)) / width_with_grab);

    data->value_changed = held;

    if (data->value_changed)
    {
        float dist_to_grab1 = std::abs(xgui->mouse_pos().x - (rect.Min.x + anim->offset[0]));
        float dist_to_grab2 = std::abs(xgui->mouse_pos().x - (rect.Min.x + anim->offset[1]));

        if (dist_to_grab1 < dist_to_grab2 && !anim->grab_active[1])
            anim->grab_active[0] = true;

        if (dist_to_grab2 < dist_to_grab1 && !anim->grab_active[0])
            anim->grab_active[1] = true;
    }
    else
    {
        anim->grab_active[0] = false;
        anim->grab_active[1] = false;
    }

    if (anim->grab_active[0])
    {
        T new_value = static_cast<T>(min + t * (max - min));
        *callback = ImClamp(new_value, min, *callback_two - range);

    }

    if (anim->grab_active[1])
    {
        T new_value = static_cast<T>(min + t * (max - min));
        *callback_two = ImClamp(new_value, *callback + range, max);

    }

    xgui->easing(anim->offset[0], grab_offset, 20.f, dynamic_easing);
    xgui->easing(anim->offset[1], grab_offset2, 20.f, dynamic_easing);

    draw->rect_filled(xgui->window_drawlist(), rect.Min + SCALE(0, elements->slider.padding), rect.Max - SCALE(0, elements->slider.padding), draw->get_clr(clr->widgets.background), rect.GetHeight() / 2);
    draw->rect(xgui->window_drawlist(), rect.Min + SCALE(0, elements->slider.padding), rect.Max - SCALE(0, elements->slider.padding), draw->get_clr(clr->widgets.stroke), rect.GetHeight() / 2, 0, SCALE(1));

    float h, s, v;
    ImVec4 color{ 1.f, 1.f, 1.f, 1.f };
    xgui->rgb_to_hsv(clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, h, s, v);

    v = std::max(0.0f, v - 0.6f);
    xgui->hsv_to_rgb(h, s, v, color.x, color.y, color.z);

    draw->rect_filled_multi_color(xgui->window_drawlist(), rect.Min + ImVec2(anim->offset[0], SCALE(elements->slider.padding)), ImVec2(rect.Min.x + anim->offset[1], rect.Max.y - SCALE(elements->slider.padding)), draw->get_clr(clr->accent), draw->get_clr(color, 1.f), draw->get_clr(color, 1.f), draw->get_clr(clr->accent), rect.GetHeight() / 2);
    draw->circle_filled(xgui->window_drawlist(), ImVec2(rect.Min.x + anim->offset[0], rect.GetCenter().y), SCALE(elements->slider.size / 2), draw->get_clr(clr->widgets.text), 30);
    draw->circle_filled(xgui->window_drawlist(), ImVec2(rect.Min.x + anim->offset[1], rect.GetCenter().y), SCALE(elements->slider.size / 2), draw->get_clr(clr->widgets.text), 30);

    T* data_ptr = static_cast<T*>(callback);
    T target_value = *data_ptr;

    if constexpr (std::is_arithmetic_v<T>) {

        T step = (target_value - anim->slow_value[0]) * xgui->fixed_speed(15.f);
        if (step == 0 && anim->slow_value[0] != target_value) step = (target_value > anim->slow_value[0]) ? 1 : -1;
        anim->slow_value[0] += step;
    }

    snprintf(value_buf1, sizeof(value_buf1), format.data(), anim->slow_value[0]);

    T* data_ptr_2 = static_cast<T*>(callback_two);
    T target_value_2 = *data_ptr_2;

    if constexpr (std::is_arithmetic_v<T>) {

        T step = (target_value_2 - anim->slow_value[1]) * xgui->fixed_speed(15.f);
        if (step == 0 && anim->slow_value[1] != target_value_2) step = (target_value_2 > anim->slow_value[1]) ? 1 : -1;
        anim->slow_value[1] += step;
    }

    snprintf(value_buf2, sizeof(value_buf2), format.data(), anim->slow_value[1]);

    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min - SCALE(0, 3), rect.GetTR(), draw->get_clr(clr->widgets.text_inactive), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));
    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min - SCALE(0, 3), rect.GetTR(), draw->get_clr(clr->widgets.text), (std::stringstream{} << value_buf1 << ", " << value_buf2).str().data(), NULL, NULL, ImVec2(1.f, 0.f));

    if (var->gui.section_alpha == 0.f || var->gui.sub_section_alpha == 0.f)
    {
        anim->offset[0] = 0.f;
        anim->offset[1] = 0.f;

    }

    return data;
}

widget_t c_widgets::range_int(std::string_view key)
{
    range_int_t* data = cfg->fill<range_int_t>(key.data());

    return range_ex(data->name, &data->callback, &data->callback_two, data->min, data->max, data->range, data->format);
}

widget_t c_widgets::range_float(std::string_view key)
{
    range_float_t* data = cfg->fill<range_float_t>(key.data());

    return range_ex(data->name, &data->callback, &data->callback_two, data->min, data->max, data->range, data->format);
}