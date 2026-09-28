#include "../headers/functions.h"
#include "../headers/widgets.h"

//
// XISFPS checkbox.
//
// Design: square, filled-magenta-when-checked, white check glyph.
// Reads at-a-glance exactly like the reference UI — a real checkbox rather
// than a toggle switch, so a screen full of them scans as a list of options.
//
widget_t checkbox_ex(std::string_view name, bool* callback, bool keybind, key_data_t* key, std::function<void()> extra_in_cog = nullptr)
{
    struct anim_t
    {
        float check_alpha{ 0.f };       // magenta fill of the checkbox
        float glow_alpha{ 0.f };        // outer glow around the checkbox when checked
        ImVec4 text{ clr->widgets.text_inactive };

        bool window_opened{ false };
        bool window_hovered{ false };
        float window_alpha{ 0 };
        float window_offset{ 0 };
        ImVec4 icon{ clr->widgets.text_inactive };
    };

    widget_t data = xgui->register_item(name, ImVec2(xgui->content_avail().x, SCALE(elements->checkbox.height)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);

    // The clickable checkbox is a small SQUARE on the right side of the row.
    const float box_size = SCALE(elements->checkbox.height);
    const ImVec2 box_min{ data->rect.Max.x - box_size, data->rect.Min.y };
    const ImVec2 box_max{ data->rect.Max.x, data->rect.Max.y };
    const ImRect rect{ box_min, box_max };

    // Keybind icon "add" hit-target (kept identical to the old layout).
    const ImRect add{ rect.Min - SCALE(elements->checkbox.add_size * 2, 0), rect.GetBL() - SCALE(elements->checkbox.add_size, 0) };
    const ImRect inline_zone{
        ImVec2(rect.Min.x - SCALE(elements->checkbox.add_size * 40), data->rect.Min.y),
        ImVec2(rect.Min.x, data->rect.Max.y)
    };

    data->value_changed = data->hovered && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered() && !inline_zone.Contains(xgui->mouse_pos());

    if (data->value_changed)
    {
        *callback = !*callback;

        std::string display(name);
        if (auto p = display.find("##"); p != std::string::npos)
            display.resize(p);

        std::string message = display + (*callback ? " enabled" : " disabled");
        xnotify->add_notify(message, *callback ? notify_type::success : notify_type::warning);
    }

    xgui->easing(anim->check_alpha, *callback ? 1.f : 0.f, 10.f, static_easing);
    xgui->easing(anim->glow_alpha,  *callback ? 1.f : 0.f, 6.f,  static_easing);
    xgui->easing(anim->text, *callback ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    // ---- box ----
    const float box_rounding = SCALE(3.f);

    // Faint outer glow when checked.
    if (anim->glow_alpha > 0.01f)
    {
        const float g = SCALE(3.f);
        draw->rect_filled(xgui->window_drawlist(),
            rect.Min - ImVec2(g, g), rect.Max + ImVec2(g, g),
            draw->get_clr(clr->accent, 0.18f * anim->glow_alpha),
            box_rounding + g);
    }

    // Unchecked base: near-black fill + subtle stroke.
    draw->rect_filled(xgui->window_drawlist(), rect.Min, rect.Max, draw->get_clr(clr->widgets.background), box_rounding);
    // Checked fill: magenta washes in over the base.
    draw->rect_filled(xgui->window_drawlist(), rect.Min, rect.Max, draw->get_clr(clr->accent, anim->check_alpha), box_rounding);

    // Border: subtle cool stroke by default, brighter magenta when checked.
    ImVec4 border = ImLerp(clr->widgets.stroke.Value,
                           ImVec4(clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, 1.f),
                           anim->check_alpha);
    draw->rect(xgui->window_drawlist(), rect.Min, rect.Max, draw->get_clr(border, 0.9f), box_rounding, 0, SCALE(1));

    // Check mark ("V" polyline). Drawn only when meaningfully visible.
    if (anim->check_alpha > 0.05f)
    {
        const ImU32 check_col = draw->get_clr(ImVec4(1.f, 1.f, 1.f, 1.f), anim->check_alpha);
        const float t = SCALE(1.8f);
        const ImVec2 c = rect.GetCenter();
        const float s = box_size * 0.28f;
        const ImVec2 p1{ c.x - s,        c.y + s * 0.05f };
        const ImVec2 p2{ c.x - s * 0.20f, c.y + s * 0.55f };
        const ImVec2 p3{ c.x + s * 0.85f, c.y - s * 0.55f };
        xgui->window_drawlist()->PathClear();
        xgui->window_drawlist()->PathLineTo(p1);
        xgui->window_drawlist()->PathLineTo(p2);
        xgui->window_drawlist()->PathLineTo(p3);
        xgui->window_drawlist()->PathStroke(check_col, ImDrawFlags_None, t);
    }

    // Label text on the LEFT (opposite side of the box).
    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14),
        data->rect.Min, ImVec2(rect.Min.x - SCALE(6.f), data->rect.Max.y),
        draw->get_clr(anim->text), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.5f));

    if (keybind)
    {
        xgui->easing(anim->icon, add.Contains(xgui->mouse_pos()) || anim->window_alpha > 0.f ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);
        xgui->easing(anim->window_alpha, anim->window_opened ? 1.f : 0.f, 8.f, static_easing);
        xgui->easing(anim->window_offset, anim->window_opened ? elements->keybind.window_padding : 0.f, 16.f, dynamic_easing);

        draw->text_clipped(xgui->window_drawlist(), font->get(icons_data, 12), add.Min, add.Max, draw->get_clr(anim->icon), "N", NULL, NULL, ImVec2(0.5f, 0.5f));

        if ((add.Contains(xgui->mouse_pos()) && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered()) || (anim->window_opened && xgui->mouse_clicked(mouse_button_left) && !anim->window_hovered))
            anim->window_opened = !anim->window_opened;

        if (anim->window_alpha >= 0.01f)
        {
            xgui->push_var(style_var_alpha, anim->window_alpha);
            xgui->push_var(style_var_window_rounding, SCALE(elements->keybind.rounding));
            xgui->push_var(style_var_popup_border_size, SCALE(1));
            xgui->push_var(style_var_window_padding, SCALE(elements->keybind.padding));
            xgui->push_var(style_var_item_spacing, SCALE(elements->keybind.spacing));
            xgui->push_color(style_col_popup_bg, draw->get_clr(clr->window.sub_background));
            xgui->push_color(style_col_border, draw->get_clr(clr->window.stroke));
            // Posicionamento auto: abre abaixo do gear por default; se não
            // caberia (checkbox perto do rodapé da janela — Aimlock é a última
            // função da child), flipa e abre acima. Evita ficar cortado sem
            // depender de scroll interno. Estimativa da altura do popup:
            // key_select (~34) + mode_select (~34) + padding*2 + spacing.
            const float popup_w = SCALE(elements->keybind.width);
            const float popup_h_est = SCALE(80.f) + SCALE(elements->keybind.padding.y) * 2.f;
            const ImGuiIO& _kbio = ImGui::GetIO();
            float pos_x = add.GetCenter().x - popup_w * 0.5f;
            float pos_y = add.Max.y + anim->window_offset;
            if (pos_y + popup_h_est > _kbio.DisplaySize.y - SCALE(4.f))
                pos_y = add.Min.y - popup_h_est - anim->window_offset;
            if (pos_x + popup_w > _kbio.DisplaySize.x - SCALE(4.f))
                pos_x = _kbio.DisplaySize.x - popup_w - SCALE(4.f);
            if (pos_x < SCALE(4.f)) pos_x = SCALE(4.f);
            if (pos_y < SCALE(4.f)) pos_y = SCALE(4.f);
            xgui->set_next_window_pos(ImVec2(pos_x, pos_y));
            xgui->set_next_window_size(SCALE(elements->keybind.width, 0));
            xgui->begin("keybind_window" + std::to_string(data->id), nullptr, window_flags_tooltip | window_flags_always_use_window_padding | window_flags_no_saved_settings | window_flags_no_focus_on_appearing | window_flags_always_auto_resize | window_flags_no_decoration | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
            {
                xgui->set_window_focus();
                anim->window_hovered = xgui->is_window_hovered();

                widgets->key_select("key_select", &key->key);

                widgets->mode_select("mode_select", &key->mode);

                if (extra_in_cog) extra_in_cog();
            }
            xgui->end();
            xgui->pop_color(2);
            xgui->pop_var(5);
        }
    }

    var->gui.color_edit_rect = add;

    if (keybind)
        var->gui.color_edit_rect = ImRect(add.Min - SCALE(elements->checkbox.add_size * 2, 0), add.GetBL() - SCALE(elements->checkbox.add_size, 0));

    if (var->gui.section_alpha == 0.f || var->gui.sub_section_alpha == 0.f)
    {
        anim->check_alpha = 0.f;
        anim->glow_alpha = 0.f;
        anim->text = clr->widgets.text_inactive;
    }

    return data;
}

widget_t c_widgets::checkbox(std::string_view key)
{
    checkbox_t* data = cfg->fill<checkbox_t>(key.data());

    return checkbox_ex(data->name, &data->callback, data->keybind, &data->key_data);
}

widget_t c_widgets::checkbox(std::string_view name, bool* value)
{
    return checkbox_ex(name, value, false, nullptr);
}

widget_t c_widgets::checkbox(std::string_view key, std::function<void()> extra_in_cog)
{
    checkbox_t* data = cfg->fill<checkbox_t>(key.data());
    return checkbox_ex(data->name, &data->callback, data->keybind, &data->key_data, std::move(extra_in_cog));
}
