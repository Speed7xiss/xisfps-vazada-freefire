#include "../headers/functions.h"
#include "../headers/widgets.h"
#include "../sections/fonts_shared.h"

widget_t c_widgets::section(std::string_view icon, int index, int& count)
{
    struct anim_t
    {
        float alpha{ 0 };
        ImVec4 icon{ clr->widgets.text_inactive };
    };

    widget_t data = xgui->register_item("section_" + std::string(icon), ImVec2(xgui->content_avail().x, xgui->content_avail().x));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);
    bool active = index == count;

    data->value_changed = data->hovered && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered();
    if (data->value_changed)
    {
        count = index;
    }

    xgui->easing(anim->alpha, active ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->icon, active ? clr->accent.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);

    ImFont* fnt = (icon.size() > 0 && (uint8_t)icon[0] >= 0x80 && fonts_shared::material_icons_font)
        ? fonts_shared::material_icons_font
        : font->get(icons_data, 12.f);

    draw->text_clipped(xgui->window_drawlist(), fnt, data->rect.Min, data->rect.Max, draw->get_clr(anim->icon), icon.data(), NULL, NULL, ImVec2(0.5f, 0.5f));

    return data;
}

//
// XISFPS textual sidebar row with an icon.
//
// Layout:
//   ┌────────────────────────────────┐
//   │  ⌖   Legit                     │   ← icon + text, both centered vertically
//   └────────────────────────────────┘
//
// The `icon` argument is a Material Icons codepoint (UTF-8) rendered with
// fonts_shared::material_icons_font. Text uses the regular UI font.
//
widget_t c_widgets::section(std::string_view icon, std::string_view name, int index, int& count)
{
    struct anim_t
    {
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text_inactive };
    };

    // Fixed row height — mirrors modern sidebar UIs. 34 logical px reads
    // well at all DPIs the app supports.
    widget_t data = xgui->register_item("section_row_" + std::string(name), ImVec2(xgui->content_avail().x, SCALE(34)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);
    const bool active = index == count;
    const bool hovered = data->hovered && xgui->is_window_hovered();

    data->value_changed = hovered && xgui->mouse_clicked(mouse_button_left);
    if (data->value_changed)
        count = index;

    xgui->easing(anim->alpha, active ? 1.f : 0.f, 8.f, static_easing);

    // MultiLoader palette — three text tones, distinct at a glance:
    //   idle    = RGB(128,128,128)  → 0.502
    //   hovered = RGB(170,170,170)  → 0.667
    //   active  = RGB(220,220,220)  → 0.863
    const ImVec4 idle_c   { 128.f/255.f, 128.f/255.f, 128.f/255.f, 1.f };
    const ImVec4 hover_c  { 170.f/255.f, 170.f/255.f, 170.f/255.f, 1.f };
    const ImVec4 active_c { 220.f/255.f, 220.f/255.f, 220.f/255.f, 1.f };
    const ImVec4 target   = active ? active_c : (hovered ? hover_c : idle_c);
    xgui->easing(anim->text, target, 16.f, dynamic_easing);
    const ImU32 label_col = draw->get_clr(anim->text);

    // ---- Icon (user-supplied Font Awesome PNGs) ----
    //   Textures are white + alpha, uploaded once in c_gui::initialize().
    //   We tint them by passing `label_col` as the AddImage vertex color:
    //     final = tex.rgba (1,1,1,α) × label_col.rgba
    //   So the icon inherits the row's text tone (dim / hover / active)
    //   automatically. Pick the texture from the tab's NAME.
    const float icon_slot_w = SCALE(22.f);
    const float text_pad_x  = SCALE(14.f);
    const float icon_side   = SCALE(14.f);   // draw box (square) — was 18, felt too big
    const ImVec2 icon_min{ data->rect.Min.x + SCALE(12.f), data->rect.Min.y };
    const ImVec2 icon_max{ icon_min.x + icon_slot_w,       data->rect.Max.y };
    const ImVec2 icon_c   { (icon_min.x + icon_max.x) * 0.5f,
                            (icon_min.y + icon_max.y) * 0.5f };
    const ImVec2 img_min  { icon_c.x - icon_side * 0.5f, icon_c.y - icon_side * 0.5f };
    const ImVec2 img_max  { icon_c.x + icon_side * 0.5f, icon_c.y + icon_side * 0.5f };

    ImTextureID tex = nullptr;
    if      (name == "Spoof")    tex = var->gui.tab_tex_spoof;
    else if (name == "Serial")   tex = var->gui.tab_tex_serial;
    else if (name == "Combat")   tex = var->gui.tab_tex_combat;
    else if (name == "Visuals")  tex = var->gui.tab_tex_visuals;
    else if (name == "Colors")   tex = var->gui.tab_tex_lists;
    else if (name == "Exploits") tex = var->gui.tab_tex_exploits;
    else if (name == "Trigger")  tex = var->gui.tab_tex_recoil_custom;
    else if (name == "Recoil")   tex = var->gui.tab_tex_trigger_custom;
    else if (name == "Skins")    tex = var->gui.tab_tex_shirt;
    else if (name == "Cloud")    tex = var->gui.tab_tex_cloud;
    else if (name == "Config")   tex = var->gui.tab_tex_config;

    if (tex)
    {
        xgui->window_drawlist()->AddImage(tex, img_min, img_max,
            ImVec2(0, 0), ImVec2(1, 1), label_col);
    }

    // ---- Label ----
    ImFont* fnt = font->get(onest_medium_data, 14);
    draw->text_clipped(xgui->window_drawlist(), fnt,
        ImVec2(icon_max.x + text_pad_x - SCALE(4.f), data->rect.Min.y),
        data->rect.Max,
        label_col, name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.5f));

    return data;
}

void c_widgets::begin_section(std::string_view name)
{
    struct anim_t
    {
        float height{ 0 };
    };

    anim_t* anim = xgui->anim_container<anim_t>(xgui->get_window()->GetID(("section_" + std::string(name)).data()));
    widget_t data = xgui->register_item("section_" + std::string(name), ImVec2(xgui->content_avail().x, SCALE(elements->sub_section.top + elements->sub_section.bottom) + anim->height));

    // XISFPS: section title text intentionally NOT drawn — the tab name
    // already lives in the sidebar; repeating it at the top of the sub-
    // sidebar was noise. The `name` string is still used as a stable ID
    // for the child container and animation state below.

    // With the section title removed, we don't need the +18 breathing room
    // that separated it from the first subsection. A tiny 4 px cushion is
    // enough so the first subsection doesn't hug the top of the sub-sidebar.
    const float SECTION_TITLE_GAP = elements->sub_section.top - 20.f;
    xgui->set_screen_pos(data->rect.Min + ImVec2(0, SCALE(SECTION_TITLE_GAP)), pos_all);
    xgui->begin_content("section_content_" + std::string(name), ImVec2(xgui->content_avail().x, anim->height), ImVec2(0, 0), SCALE(0, 0), window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);

    anim->height = xgui->get_window()->ContentSize.y;

}

void c_widgets::end_section()
{
    xgui->end_content();
}

bool c_widgets::begin_sub_section(std::string_view name)
{
    struct anim_t
    {
        float height{ 0 };
        float anim_height{ 1 };
        float data_height{ 3 };
        float alpha{ 0 };
        float rotate{ 0 };
        bool opened{ true };
        ImVec4 text{ clr->widgets.text_inactive };
        ImVec4 icon{ clr->widgets.text_inactive };
    };

    // XISFPS: simplified sub-section header.
    //  - No more "L" leading glyph — the group name stands on its own.
    //  - No more vertical stem decoration linking header to child items.
    //  - A chevron on the right rotates on toggle, in accent color.
    const float row_h = xgui->text_size(font->get(onest_medium_data, 14), "Xg").y;

    anim_t* anim = xgui->anim_container<anim_t>(xgui->get_window()->GetID(("sub_section_" + std::string(name)).data()));
    widget_t data = xgui->register_item("sub_section_" + std::string(name), ImVec2(xgui->content_avail().x, row_h + anim->data_height));

    var->gui.sub_pos = data->rect.GetBL() + SCALE(0, elements->sub_section.item_spacing);

    xgui->easing(anim->anim_height, anim->opened ? anim->height : 1.f, 16.f, dynamic_easing);
    xgui->easing(anim->data_height, anim->opened ? anim->height + SCALE(elements->sub_section.padding.y) : 3.f, 16.f, dynamic_easing);
    xgui->easing(anim->alpha, anim->opened ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->rotate, anim->opened ? 1.f : 0.f, 16.f, dynamic_easing);
    // MultiLoader palette on the collapsible group header — dim grey normally,
    // brightens on open. Chevron is muted grey (matches text tone).
    const ImVec4 group_text_col = anim->opened
        ? ImVec4(200.f/255.f, 200.f/255.f, 200.f/255.f, 1.f)
        : ImVec4(128.f/255.f, 128.f/255.f, 128.f/255.f, 1.f);
    xgui->easing(anim->text, group_text_col, 16.f, dynamic_easing);
    xgui->easing(anim->icon, ImVec4(170.f/255.f, 170.f/255.f, 170.f/255.f, 1.f), 16.f, dynamic_easing);

    if (ImRect{ data->rect.Min, data->rect.GetTR() + ImVec2(0, row_h) }.Contains(xgui->mouse_pos()) && xgui->mouse_clicked(mouse_button_left))
        anim->opened = !anim->opened;

    // Group name on the left, muted-grey chevron on the right.
    draw->text_clipped(var->gui.drawlist, font->get(onest_medium_data, 13),
        data->rect.Min - SCALE(0, 1),
        ImVec2(data->rect.Max.x - SCALE(18), data->rect.Min.y + row_h),
        draw->get_clr(anim->text), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));

    draw->rotate_start(var->gui.drawlist);
    draw->text_clipped(var->gui.drawlist, font->get(icons_data, 5),
        ImVec2(data->rect.Max.x - SCALE(14), data->rect.Min.y),
        ImVec2(data->rect.Max.x, data->rect.Min.y + row_h),
        draw->get_clr(anim->icon), "M", NULL, NULL, ImVec2(0.5f, 0.5f));
    draw->rotate_end(var->gui.drawlist, -90 + (180 * anim->rotate));

    if (anim->alpha >= 0.01f)
    {
        // Children are indented by a small padding so grouped items read as a
        // set. Vertical gap under the group name is padded (was `sub_section.top`
        // = 25 → +10) so the first item doesn't touch the group header.
        const float GROUP_HEADER_GAP = elements->sub_section.top + 10.f;
        xgui->set_screen_pos(data->rect.Min + ImVec2(SCALE(elements->sub_section.padding.x), SCALE(GROUP_HEADER_GAP)), pos_all);
        xgui->push_var(style_var_alpha, anim->alpha * var->gui.section_alpha);
        xgui->begin_content("sub_section_content_" + std::string(name), ImVec2(xgui->content_avail().x, anim->anim_height), SCALE(0, 0), SCALE(0, elements->sub_section.padding.y), window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);

        anim->height = xgui->get_window()->ContentSize.y;

        return true;
    }

    return false;
}

void c_widgets::end_sub_section()
{
    xgui->end_content();
    xgui->pop_var();
}

// Shared helper — three-tone MultiLoader text easing.
static ImVec4 sub_row_text(bool active, bool hovered)
{
    return active   ? ImVec4(220.f/255.f, 220.f/255.f, 220.f/255.f, 1.f)
         : hovered  ? ImVec4(170.f/255.f, 170.f/255.f, 170.f/255.f, 1.f)
                    : ImVec4(128.f/255.f, 128.f/255.f, 128.f/255.f, 1.f);
}

widget_t c_widgets::sub_section_list(std::string_view name, int index, int& count)
{
    struct anim_t
    {
        ImVec4 text{ 128.f/255.f, 128.f/255.f, 128.f/255.f, 1.f };
        float bar_alpha{ 0 };
    };

    widget_t data = xgui->register_item("sub_section_l_" + std::string(name), ImVec2(xgui->content_avail().x, SCALE(14)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);
    const bool active  = index == count;
    const bool hovered = data->hovered && xgui->is_window_hovered();

    data->value_changed = hovered && xgui->mouse_clicked(mouse_button_left);
    if (data->value_changed)
        count = index;

    xgui->easing(anim->text, sub_row_text(active, hovered), 16.f, dynamic_easing);
    xgui->easing(anim->bar_alpha, active ? 1.f : 0.f, 12.f, static_easing);

    // MultiLoader stripe: WHITE (not accent), 2.5px, sits 6 px left of the row.
    if (anim->bar_alpha > 0.01f)
    {
        draw->rect_filled(var->gui.drawlist,
            ImVec2(data->rect.Min.x - SCALE(6), data->rect.Min.y + SCALE(1)),
            ImVec2(data->rect.Min.x - SCALE(3.5f), data->rect.Max.y - SCALE(1)),
            draw->get_clr(ImVec4(220.f/255.f, 220.f/255.f, 220.f/255.f, anim->bar_alpha)),
            SCALE(1.5f));
    }

    draw->text_clipped(var->gui.drawlist, font->get(onest_medium_data, 13),
        data->rect.Min + SCALE(4, 0) - SCALE(0, 2), data->rect.Max,
        draw->get_clr(anim->text), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));

    return data;
}

widget_t c_widgets::sub_section(std::string_view name, int index, int& count, bool lua_section)
{
    struct anim_t
    {
        ImVec4 text{ 128.f/255.f, 128.f/255.f, 128.f/255.f, 1.f };
        float bar_alpha{ 0 };
    };

    widget_t data = xgui->register_item("sub_section_b_" + std::string(name), ImVec2(xgui->content_avail().x, SCALE(14)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);
    const bool active  = index == count;
    const bool hovered = data->hovered && xgui->is_window_hovered();

    data->value_changed = hovered && xgui->mouse_clicked(mouse_button_left);
    if (data->value_changed)
        count = index;

    xgui->easing(anim->text, sub_row_text(active, hovered), 16.f, dynamic_easing);
    xgui->easing(anim->bar_alpha, active ? 1.f : 0.f, 12.f, static_easing);

    if (anim->bar_alpha > 0.01f)
    {
        draw->rect_filled(var->gui.drawlist,
            ImVec2(data->rect.Min.x - SCALE(6), data->rect.Min.y + SCALE(1)),
            ImVec2(data->rect.Min.x - SCALE(3.5f), data->rect.Max.y - SCALE(1)),
            draw->get_clr(ImVec4(220.f/255.f, 220.f/255.f, 220.f/255.f, anim->bar_alpha)),
            SCALE(1.5f));
    }

    draw->text_clipped(var->gui.drawlist, font->get(onest_medium_data, 13),
        data->rect.Min - SCALE(0, 2), data->rect.Max,
        draw->get_clr(anim->text), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.f));

    var->gui.sub_pos = data->rect.GetBL() + SCALE(0, elements->sub_section.item_spacing);

    return data;
}