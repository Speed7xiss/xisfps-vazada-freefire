#include "../headers/functions.h"
#include "../headers/widgets.h"

bool c_widgets::cog(std::string_view id, std::function<void()> body)
{
    struct anim_t
    {
        bool   window_opened { false };
        bool   window_hovered{ false };
        float  window_alpha  { 0.f };
        float  window_offset { 0.f };
        ImVec4 icon          { clr->widgets.text_inactive };
    };

    ImRect cog_rect = var->gui.color_edit_rect;

    ImGuiID gid = xgui->get_window()->GetID(("cog_" + std::string(id)).data());
    anim_t* anim = xgui->anim_container<anim_t>(gid);

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

        xgui->begin("cog_window" + std::to_string(gid), nullptr, window_flags_tooltip | window_flags_always_use_window_padding | window_flags_no_saved_settings | window_flags_no_focus_on_appearing | window_flags_always_auto_resize | window_flags_no_decoration | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
        {
            xgui->set_window_focus();
            anim->window_hovered = xgui->is_window_hovered();

            if (body) body();
        }
        xgui->end();

        xgui->pop_color(2);
        xgui->pop_var(5);
    }

    return false;
}
