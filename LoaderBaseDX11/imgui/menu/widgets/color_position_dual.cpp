#include "../headers/functions.h"
#include "../headers/widgets.h"

bool c_widgets::color_position_dual(std::string_view key, std::string_view extra_checkbox_key)
{
    struct anim_t
    {
        bool   window_opened { false };
        bool   window_hovered{ false };
        float  window_alpha  { 0.f };
        float  window_offset { 0.f };
        ImVec4 icon          { clr->widgets.text_inactive };
    };

    std::string vis_key   = std::string(key) + "##vis";
    std::string invis_key = std::string(key) + "##invis";
    std::string pos_key   = std::string(key) + "##pos";

    auto* preview_cb = cfg->fill<checkbox_t>("ESP Preview");
    bool preview_on = preview_cb && preview_cb->callback;

    if (preview_on)
    {

        ImRect invis_rect_local = var->gui.color_edit_rect;
        ImRect vis_rect_local   = ImRect(invis_rect_local.Min - SCALE(elements->checkbox.add_size * 2, 0), invis_rect_local.GetBL() - SCALE(elements->checkbox.add_size, 0));

        var->gui.color_edit_rect = vis_rect_local;
        bool changed_vis_p = color_edit(vis_key);
        var->gui.color_edit_rect = invis_rect_local;
        bool changed_invis_p = color_edit(invis_key);
        return changed_vis_p || changed_invis_p;
    }

    ImRect cog_rect = var->gui.color_edit_rect;

    ImRect invis_rect = ImRect(cog_rect.Min - SCALE(elements->checkbox.add_size * 2, 0), cog_rect.GetBL() - SCALE(elements->checkbox.add_size, 0));
    ImRect vis_rect   = ImRect(invis_rect.Min - SCALE(elements->checkbox.add_size * 2, 0), invis_rect.GetBL() - SCALE(elements->checkbox.add_size, 0));

    var->gui.color_edit_rect = vis_rect;
    bool changed_vis = color_edit(vis_key);

    var->gui.color_edit_rect = invis_rect;
    bool changed_invis = color_edit(invis_key);

    ImGuiID id  = xgui->get_window()->GetID(("cfg_dual_" + std::string(key)).data());
    anim_t* anim = xgui->anim_container<anim_t>(id);

    bool hovered_cog = cog_rect.Contains(xgui->mouse_pos()) && xgui->is_window_hovered();

    if ((hovered_cog && xgui->mouse_clicked(mouse_button_left))
     || (anim->window_opened && xgui->mouse_clicked(mouse_button_left) && !anim->window_hovered))
        anim->window_opened = !anim->window_opened;

    xgui->easing(anim->icon, (hovered_cog || anim->window_alpha > 0.f) ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);
    xgui->easing(anim->window_alpha,  anim->window_opened ? 1.f : 0.f, 8.f,  static_easing);
    xgui->easing(anim->window_offset, anim->window_opened ? elements->keybind.window_padding : 0.f, 16.f, dynamic_easing);

    draw->text_clipped(xgui->window_drawlist(), font->get(icons_data, 12), cog_rect.Min, cog_rect.Max, draw->get_clr(anim->icon), "N", NULL, NULL, ImVec2(0.5f, 0.5f));

    if (anim->window_alpha >= 0.01f)
    {
        xgui->push_var(style_var_alpha,              anim->window_alpha);
        xgui->push_var(style_var_window_rounding,    SCALE(elements->keybind.rounding));
        xgui->push_var(style_var_popup_border_size,  SCALE(1));
        xgui->push_var(style_var_window_padding,     SCALE(elements->keybind.padding));
        xgui->push_var(style_var_item_spacing,       SCALE(elements->keybind.spacing));
        xgui->push_color(style_col_popup_bg, draw->get_clr(clr->window.sub_background));
        xgui->push_color(style_col_border,   draw->get_clr(clr->window.stroke));

        xgui->set_next_window_pos(ImVec2(cog_rect.GetCenter().x - SCALE(elements->keybind.width / 2), cog_rect.Max.y + anim->window_offset));
        xgui->set_next_window_size(SCALE(elements->keybind.width, 0));

        xgui->begin("cfg_window" + std::to_string(id), nullptr, window_flags_tooltip | window_flags_always_use_window_padding | window_flags_no_saved_settings | window_flags_no_focus_on_appearing | window_flags_always_auto_resize | window_flags_no_decoration | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
        {
            xgui->set_window_focus();
            anim->window_hovered = xgui->is_window_hovered();

            widgets->dropdown(pos_key);

            if (!extra_checkbox_key.empty())
                widgets->checkbox(extra_checkbox_key);
        }
        xgui->end();

        xgui->pop_color(2);
        xgui->pop_var(5);
    }

    return changed_vis || changed_invis;
}
