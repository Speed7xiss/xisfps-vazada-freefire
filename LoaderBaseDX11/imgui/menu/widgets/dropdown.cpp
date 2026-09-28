#include "../headers/functions.h"
#include "../headers/widgets.h"

widget_t selectable(std::string_view name, bool active)
{
    struct anim_t
    {
        float alpha{ 0 };            // active-fill alpha (kept for background wash)
        float hover_alpha{ 0 };      // separate track so hover reads without needing "active"
        float bar_alpha{ 0 };        // left accent bar fades in only for active items
        ImVec4 text{ clr->widgets.text_inactive };
    };

    widget_t data = xgui->register_item(name, ImVec2(xgui->content_avail().x, SCALE(elements->selectable.height)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);
    data->value_changed = data->hovered && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered();

    const bool hovered = data->hovered && xgui->is_window_hovered();

    xgui->easing(anim->alpha,       active  ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->hover_alpha, hovered ? 1.f : 0.f, 10.f, static_easing);
    xgui->easing(anim->bar_alpha,   active  ? 1.f : 0.f, 12.f, static_easing);
    xgui->easing(anim->text, (active || hovered) ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    // Row background: hover uses a low-alpha widget wash; active adds a magenta tint on top.
    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background, 0.55f * anim->hover_alpha));
    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->accent, 0.12f * anim->alpha));

    // Left accent bar on the active item — visual echo of the sidebar section pill.
    if (anim->bar_alpha > 0.01f)
    {
        const float bar_w = SCALE(3.f);
        const float bar_pad = SCALE(4.f);
        draw->rect_filled(xgui->window_drawlist(),
            ImVec2(data->rect.Min.x, data->rect.Min.y + bar_pad),
            ImVec2(data->rect.Min.x + bar_w, data->rect.Max.y - bar_pad),
            draw->get_clr(clr->accent, anim->bar_alpha));
    }

    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min + SCALE(elements->selectable.padding, 0), data->rect.Max, draw->get_clr(anim->text), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.5f));

    return data;
}

bool begin_dropdown(std::string_view name, std::string& preview, int val, widget_t& data, bool multi = false)
{
    struct anim_t
    {
        bool opened{ false };
        bool hovered{ false };
        int stored_val{ -1 };
        float alpha{ 0 };
        float rotate{ 0 };
        float offset{ 0 };
    };

    data = xgui->register_item(name, ImVec2(xgui->content_avail().x, SCALE(elements->dropdown.height)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);
    const ImRect icon{ data->rect.GetBR() - ImVec2(xgui->text_size(font->get(icons_data, 6), "M").x + SCALE(elements->dropdown.padding * 2), SCALE(elements->dropdown.size)), data->rect.Max };
    const ImRect rect{ ImVec2(data->rect.Min.x, icon.Min.y), icon.GetBL() };

    if (anim->stored_val < 0)
        anim->stored_val = val;

    val = val > 4 ? 4 : val;

    if (anim->opened)
    {
        if (xgui->mouse_clicked(mouse_button_left))
            anim->opened = false;
    }
    else
    {
        if (ImRect{ rect.Min, icon.Max }.Contains(xgui->mouse_pos()) && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered())
            anim->opened = true;
    }

    xgui->easing(anim->alpha, anim->opened ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->rotate, anim->opened ? 1.f : 0.f, 16.f, dynamic_easing);
    xgui->easing(anim->offset, anim->opened ? elements->dropdown.window_padding : 0.f, 16.f, dynamic_easing);

    // XISFPS dropdown field:
    //  - Border swaps to muted accent while the popup is open, mirroring the
    //    button hover cue and giving a clear "this control owns the popup"
    //    read even if the mouse leaves.
    ImVec4 stroke_col = anim->opened
        ? ImVec4(clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, 0.55f)
        : clr->widgets.stroke.Value;

    draw->rect_filled(xgui->window_drawlist(), rect.Min, icon.Max, draw->get_clr(clr->widgets.background), SCALE(elements->dropdown.rounding));
    // Very subtle magenta wash while open.
    draw->rect_filled(xgui->window_drawlist(), rect.Min, icon.Max, draw->get_clr(clr->accent, 0.06f * anim->alpha), SCALE(elements->dropdown.rounding));
    draw->rect(xgui->window_drawlist(), rect.Min, icon.Max, draw->get_clr(stroke_col), SCALE(elements->dropdown.rounding), 0, SCALE(1));

    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min - SCALE(0, 3), rect.GetTR(), draw->get_clr(clr->widgets.text_inactive), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));
    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), rect.Min + SCALE(elements->dropdown.padding, 0), icon.GetBL(), draw->get_clr(clr->widgets.text), preview.data(), NULL, NULL, ImVec2(0.0f, 0.5f));

    // Chevron tints toward accent while open so the rotation reads with two cues (rotation + color).
    ImVec4 chevron_col = ImLerp(clr->widgets.text.Value, clr->accent.Value, anim->alpha * 0.75f);
    draw->rotate_start(xgui->window_drawlist());
    draw->text_clipped(xgui->window_drawlist(), font->get(icons_data, 6), icon.Min, icon.Max, draw->get_clr(chevron_col), "M", NULL, NULL, ImVec2(0.5f, 0.5f));
    draw->rotate_end(xgui->window_drawlist(), 90.f - (180.f * anim->rotate));

    if (anim->alpha <= 0.01f || !xgui->is_rect_visible(ImRect{ rect.GetBL(), icon.GetBL() + SCALE(0, 2) }))
    {
        anim->hovered = false;

        return false;
    }

    xgui->push_var(style_var_alpha, anim->alpha);
    xgui->push_var(style_var_window_rounding, SCALE(elements->dropdown.rounding));
    xgui->push_var(style_var_popup_border_size, SCALE(1));
    xgui->push_var(style_var_window_padding, ImVec2(0, 0));
    xgui->push_var(style_var_item_spacing, ImVec2(0, 0));
    xgui->push_var(style_var_scrollbar_content_padding, SCALE(elements->dropdown.scrollbar_content));
    xgui->push_var(style_var_scrollbar_border_padding, SCALE(elements->dropdown.scrollbar_border));
    xgui->push_color(style_col_popup_bg, draw->get_clr(clr->window.sub_background));
    xgui->push_color(style_col_border, draw->get_clr(clr->window.stroke));
    xgui->set_next_window_pos(rect.GetBL() + SCALE(0, anim->offset));
    xgui->set_next_window_size(ImVec2(data->rect.GetWidth(), SCALE(elements->selectable.height * val)));
    xgui->begin("dropdown_window" + std::to_string(data->id), nullptr, window_flags_tooltip | window_flags_always_use_window_padding | window_flags_no_saved_settings | window_flags_no_focus_on_appearing | window_flags_always_auto_resize | window_flags_no_title_bar | window_flags_no_resize | window_flags_no_collapse | (anim->stored_val <= 4 ? (window_flags_no_scrollbar | window_flags_no_scroll_with_mouse) : window_flags_always_vertical_scrollbar));

    xgui->set_window_focus();
    anim->hovered = xgui->is_window_hovered();

    return true;
}

void end_dropdown()
{
    xgui->end();
    xgui->pop_color(2);
    xgui->pop_var(7);
}

widget_t c_widgets::dropdown(std::string_view key)
{
    dropdown_t* data = cfg->fill<dropdown_t>(key.data());
    widget_t item_data;

    std::string preview{ data->items.at(data->callback) };

    if (begin_dropdown(data->name, preview, data->items.size(), item_data))
    {
        for (int i = 0; i < data->items.size(); ++i)
        {
            if (selectable(data->items.at(i), data->callback == i).value_changed)
            {
                data->callback = i;
                item_data->value_changed = xgui->is_item_clicked(mouse_button_left);
            }
        }

        end_dropdown();
    }

    return item_data;
}

widget_t c_widgets::multi_dropdown(std::string_view key)
{
    multi_dropdown_t* data = cfg->fill<multi_dropdown_t>(key.data());
    widget_t item_data;

    std::string preview = "...";

    for (size_t i = 0; i < data->items.size(); ++i)
    {
        if (data->callback.at(i))
        {
            if (preview == "...")
                preview = data->items[i];
            else
                preview += (", ") + data->items[i];
        }
    }

    if (begin_dropdown(data->name, preview, data->items.size(), item_data, true))
    {
        for (size_t i = 0; i < data->items.size(); ++i)
        {
            if (selectable(data->items.at(i), data->callback.at(i)).value_changed)
            {
                data->callback.at(i) = !data->callback.at(i);
                item_data->value_changed = xgui->is_item_clicked(mouse_button_left);
            }
        }
        end_dropdown();
    }

    preview = ("...");

    return item_data;
}