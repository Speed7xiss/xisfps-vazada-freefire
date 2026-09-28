#include "../headers/functions.h"
#include "../headers/widgets.h"

widget_t c_widgets::button_keybind(std::string_view label, std::string_view bind_key)
{
    struct anim_t
    {
        float  alpha{ 0 };
        bool   active{ false };
        ImVec4 text{ clr->widgets.text_inactive };

        bool   window_opened{ false };
        bool   window_hovered{ false };
        float  window_alpha{ 0 };
        float  window_offset{ 0 };
        ImVec4 icon{ clr->widgets.text_inactive };
    };

    checkbox_t* bind = cfg->fill<checkbox_t>(bind_key.data());

    widget_t data = xgui->register_item(label, ImVec2(xgui->content_avail().x, SCALE(elements->button.height)));
    anim_t* anim  = xgui->anim_container<anim_t>(data->id);

    const ImRect cog_rect{
        ImVec2(data->rect.Max.x - SCALE(elements->checkbox.add_size * 2), data->rect.Min.y),
        data->rect.Max
    };
    const ImRect btn_rect{
        data->rect.Min,
        ImVec2(cog_rect.Min.x, data->rect.Max.y)
    };

    bool btn_hovered = btn_rect.Contains(xgui->mouse_pos()) && xgui->is_window_hovered();
    bool cog_hovered = cog_rect.Contains(xgui->mouse_pos()) && xgui->is_window_hovered();

    bool clicked = btn_hovered && xgui->mouse_clicked(mouse_button_left);

    data->value_changed = clicked;

    if (clicked || (btn_hovered && xgui->mouse_down(mouse_button_left)))
        anim->active = true;

    xgui->easing(anim->alpha, btn_hovered && !anim->active ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->text,  btn_hovered && !anim->active ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    if (anim->alpha <= 0.01f) anim->active = false;

    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->button.rounding));
    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.sub_stroke, 0.3f * anim->alpha), SCALE(elements->button.rounding));
    draw->rect       (xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->button.rounding), 0, SCALE(1));

    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), btn_rect.Min, btn_rect.Max, draw->get_clr(anim->text), label.data(), xgui->text_end(label.data()), NULL, ImVec2(0.5f, 0.5f));

    if (!bind) return data;

    xgui->easing(anim->icon,          (cog_hovered || anim->window_alpha > 0.f) ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);
    xgui->easing(anim->window_alpha,  anim->window_opened ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->window_offset, anim->window_opened ? elements->keybind.window_padding : 0.f, 16.f, dynamic_easing);

    draw->text_clipped(xgui->window_drawlist(), font->get(icons_data, 12), cog_rect.Min, cog_rect.Max, draw->get_clr(anim->icon), "N", NULL, NULL, ImVec2(0.5f, 0.5f));

    if ((cog_hovered && xgui->mouse_clicked(mouse_button_left)) ||
        (anim->window_opened && xgui->mouse_clicked(mouse_button_left) && !anim->window_hovered))
    {
        anim->window_opened = !anim->window_opened;
    }

    if (anim->window_alpha >= 0.01f)
    {
        xgui->push_var(style_var_alpha, anim->window_alpha);
        xgui->push_var(style_var_window_rounding, SCALE(elements->keybind.rounding));
        xgui->push_var(style_var_popup_border_size, SCALE(1));
        xgui->push_var(style_var_window_padding, SCALE(elements->keybind.padding));
        xgui->push_var(style_var_item_spacing, SCALE(elements->keybind.spacing));
        xgui->push_color(style_col_popup_bg, draw->get_clr(clr->window.sub_background));
        xgui->push_color(style_col_border,   draw->get_clr(clr->window.stroke));
        xgui->set_next_window_pos(ImVec2(cog_rect.GetCenter().x - SCALE(elements->keybind.width / 2), cog_rect.Max.y + anim->window_offset));
        xgui->set_next_window_size(SCALE(elements->keybind.width, 0));
        xgui->begin("keybind_window" + std::to_string(data->id), nullptr,
            window_flags_tooltip | window_flags_always_use_window_padding | window_flags_no_saved_settings |
            window_flags_no_focus_on_appearing | window_flags_always_auto_resize | window_flags_no_decoration |
            window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
        {
            xgui->set_window_focus();
            anim->window_hovered = xgui->is_window_hovered();

            widgets->key_select("key_select",   &bind->key_data.key);
            widgets->mode_select("mode_select", &bind->key_data.mode);
        }
        xgui->end();
        xgui->pop_color(2);
        xgui->pop_var(5);
    }

    return data;
}
