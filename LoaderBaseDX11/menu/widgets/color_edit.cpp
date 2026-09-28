#include "../headers/functions.h"
#include "../headers/widgets.h"
#include <wtypes.h>

widget_t picker_button(std::string_view name, bool active)
{
    struct anim_t
    {
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text_inactive };
    };

    widget_t data = xgui->register_item(name, ImVec2(xgui->content_avail().x, SCALE(elements->color_edit.button_height)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);

    data->value_changed = data->hovered && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered();

    xgui->easing(anim->alpha, active ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->text, active ? clr->widgets.background.Value : clr->widgets.text.Value, 16.f, dynamic_easing);

    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->color_edit.content_rounding));
    draw->rect(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->color_edit.content_rounding), 0, SCALE(1));
    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->accent, anim->alpha), SCALE(elements->color_edit.content_rounding));
    draw->text_clipped(xgui->window_drawlist(), font->get(icons_data, 10), data->rect.Min, data->rect.Max, draw->get_clr(anim->text), "O", NULL, NULL, ImVec2(0.5f, 0.5f));

    return data;
}

widget_t type_button(std::string_view name)
{
    struct anim_t
    {
        float active{ false };
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text_inactive };
    };

    widget_t data = xgui->register_item(name, ImVec2(xgui->content_avail().x, SCALE(elements->color_edit.button_height)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);

    data->value_changed = data->hovered && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered();

    if (data->value_changed || data->hovered && xgui->mouse_down(mouse_button_left) && xgui->is_window_hovered())
        anim->active = true;

    xgui->easing(anim->alpha, anim->active ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->text, anim->active ? clr->widgets.background.Value : clr->widgets.text.Value, 16.f, dynamic_easing);

    if (anim->alpha >= 0.99f)
        anim->active = false;

    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), SCALE(elements->color_edit.content_rounding));
    draw->rect(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.stroke), SCALE(elements->color_edit.content_rounding), 0, SCALE(1));
    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->accent, anim->alpha), SCALE(elements->color_edit.content_rounding));
    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min, data->rect.Max, draw->get_clr(anim->text), name.data(), NULL, NULL, ImVec2(0.5f, 0.5f));

    return data;
}

bool input_hex(const char* label, float col[4], bool alpha, float width)
{
    char buf[9];

    if (alpha)
        snprintf(buf, sizeof(buf), "%02X%02X%02X%02X",
            (int)(col[0] * 255.0f),
            (int)(col[1] * 255.0f),
            (int)(col[2] * 255.0f),
            (int)(col[3] * 255.0f));
    else
        snprintf(buf, sizeof(buf), "%02X%02X%02X",
            (int)(col[0] * 255.0f),
            (int)(col[1] * 255.0f),
            (int)(col[2] * 255.0f));

    bool value_changed = widgets->hex_field(label, buf, sizeof(buf), width).value_changed;

    if (value_changed)
    {
        if (buf[0] == '#')
        {
            memmove(buf, buf + 1, strlen(buf) + 1);
        }

        for (int i = 0; i < (int)strlen(buf); ++i)
        {
            if (!isxdigit((unsigned char)buf[i]))
            {
                buf[i] = 'F';
            }
        }

        int expected_len = alpha ? 8 : 6;
        size_t current_len = strlen(buf);
        if (current_len < (size_t)expected_len)
        {
            for (size_t i = current_len; i < (size_t)expected_len; ++i)
                buf[i] = 'F';
            buf[expected_len] = '\0';
        }

        int r, g, b, a = 255;
        if (alpha)
            sscanf(buf, "%02X%02X%02X%02X", &r, &g, &b, &a);
        else
            sscanf(buf, "%02X%02X%02X", &r, &g, &b);

        col[0] = r / 255.0f;
        col[1] = g / 255.0f;
        col[2] = b / 255.0f;
        col[3] = a / 255.0f;
    }

    return value_changed;
}

void color_edit_ex(std::string_view name, float* color, bool alpha, const ImRect& rect, const ImRect& clickable_rect, bool* value_changed)
{
    struct anim_t
    {
        bool opened{ false };
        bool hovered{ false };
        float alpha{ 0.f };
        float offset{ 0 };
        float h{ -1 }, s{ -1 }, v{ -1 };
        float grab[4]{ 0.f };
        bool update_hsv{ false };
        bool pick_active{ false };
        ImVec2 end_pos{};
        int type{ 0 };
    };

    anim_t* anim = xgui->anim_container<anim_t>(xgui->get_window()->GetID(name.data()));
    ImColor col_hues[7] = { ImColor(255, 0, 0), ImColor(255, 255, 0), ImColor(0, 255, 0), ImColor(0, 255, 255), ImColor(0, 0, 255), ImColor(255, 0, 255), ImColor(255, 0, 0) };

    if ((clickable_rect.Contains(xgui->mouse_pos()) && xgui->mouse_clicked(mouse_button_left)) && xgui->is_window_hovered() || (anim->opened && (xgui->mouse_clicked(mouse_button_left) || xgui->mouse_clicked(mouse_button_right)) && !anim->hovered && !anim->pick_active))
        anim->opened = !anim->opened;

    xgui->easing(anim->alpha, anim->opened ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->offset, anim->opened ? elements->color_edit.window_padding : 0.f, 16.f, dynamic_easing);

    // XISFPS: swatch drawn as a small SQUARE (matches the reference project's
    // ImGui::ColorEdit4 default swatch, not the old rounded-glyph). Border on
    // top so it reads on both dark and light backgrounds.
    //
    // We inset by 2 px so the swatch has some padding from `rect` (which is
    // the click hit-rect, larger than the visible chip). Alpha checkerboard
    // is faked by painting a solid dark square under a translucent color.
    {
        const float inset = SCALE(2.f);
        const ImVec2 sw_min{ rect.Min.x + inset, rect.Min.y + inset };
        const ImVec2 sw_max{ rect.Max.x - inset, rect.Max.y - inset };
        const float sw_r   = SCALE(2.f);

        // Dark background (visible as the "alpha" of the swatch when color is translucent).
        draw->rect_filled(xgui->window_drawlist(), sw_min, sw_max,
            draw->get_clr(clr->widgets.background), sw_r);
        // The color itself, honoring alpha.
        draw->rect_filled(xgui->window_drawlist(), sw_min, sw_max,
            draw->get_clr({ color[0], color[1], color[2], color[3] }), sw_r);
        // Thin border.
        draw->rect(xgui->window_drawlist(), sw_min, sw_max,
            draw->get_clr(clr->widgets.stroke), sw_r, 0, SCALE(1));
    }

    xgui->rgb_to_hsv(color[0], color[1], color[2], anim->h, anim->s, anim->v);

    if (anim->alpha >= 0.01f)
    {
        xgui->push_var(style_var_alpha, anim->alpha);
        xgui->push_var(style_var_window_rounding, SCALE(elements->color_edit.rounding));
        xgui->push_var(style_var_popup_border_size, SCALE(1));
        xgui->push_var(style_var_window_padding, SCALE(elements->color_edit.padding));
        xgui->push_var(style_var_item_spacing, SCALE(elements->color_edit.spacing));
        xgui->push_color(style_col_popup_bg, draw->get_clr(clr->window.sub_background));
        xgui->push_color(style_col_border, draw->get_clr(clr->window.stroke));
        // Posiciona o popup abaixo do swatch; se não coubesse (linha perto
        // do rodapé da janela), abre acima. Evita vazar para fora da janela
        // como acontecia antes em cheats de janela pequena (720x460).
        const float popup_w = SCALE(elements->color_edit.width);
        // Estimativa conservadora da altura do popup: título + SV rect
        // (agora 90px) + barra de hue + alpha + spacing/padding.
        const float popup_h = SCALE(elements->color_edit.title + 90.f + 60.f);
        const ImGuiIO& _io  = ImGui::GetIO();
        float pos_x = rect.GetCenter().x - popup_w * 0.5f;
        float pos_y = rect.Max.y + SCALE(anim->offset);
        if (pos_y + popup_h > _io.DisplaySize.y - SCALE(4.f))
            pos_y = rect.Min.y - popup_h - SCALE(anim->offset);
        if (pos_x + popup_w > _io.DisplaySize.x - SCALE(4.f))
            pos_x = _io.DisplaySize.x - popup_w - SCALE(4.f);
        if (pos_x < SCALE(4.f)) pos_x = SCALE(4.f);
        if (pos_y < SCALE(4.f)) pos_y = SCALE(4.f);
        xgui->set_next_window_pos(ImVec2(pos_x, pos_y));
        xgui->set_next_window_size(SCALE(elements->color_edit.width, 0));
        xgui->begin("coloredit_window" + std::string(name), nullptr, window_flags_tooltip | window_flags_always_use_window_padding | window_flags_no_saved_settings | window_flags_no_focus_on_appearing | window_flags_always_auto_resize | window_flags_no_decoration | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
        {
            xgui->set_window_focus();
            anim->hovered = xgui->is_window_hovered();

            xgui->dummy(ImVec2(xgui->content_avail().x, SCALE(elements->color_edit.title - elements->color_edit.spacing.y)));
            draw->rect_filled(xgui->window_drawlist(), xgui->window_pos(), xgui->window_pos() + ImVec2(xgui->window_width(), SCALE(elements->color_edit.title)), draw->get_clr(clr->widgets.background), SCALE(elements->color_edit.rounding), draw_flags_round_corners_top);
            draw->line(xgui->window_drawlist(), xgui->window_pos() + SCALE(0, elements->color_edit.title - 1), xgui->window_pos() + ImVec2(xgui->window_width(), SCALE(elements->color_edit.title) - 1), draw->get_clr(clr->widgets.stroke), SCALE(1));
            draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), xgui->window_pos() + SCALE(elements->color_edit.padding.x, 0), xgui->window_pos() + ImVec2(xgui->window_width(), SCALE(elements->color_edit.title)), draw->get_clr(clr->widgets.text), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.f, 0.5f));

            // Antes: `content_avail().x × content_avail().x` — quadrado que
            // seguia o width da janela e fazia o popup ficar altíssimo.
            // Agora: retângulo com altura fixa de 90px — cabe tranquilo em
            // janelas pequenas sem perder usabilidade (SV picker continua
            // navegável, só mais compacto verticalmente).
            xgui->invisible_button("sv_rect", ImVec2(xgui->content_avail().x, SCALE(90.f)));

            if (xgui->is_item_active())
            {
                anim->s = ImSaturate((xgui->mouse_pos().x - (GImGui->LastItemData.Rect.Min.x + SCALE(elements->color_edit.circle_size) / 2)) / (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.circle_size)));
                anim->v = 1.f - ImSaturate((xgui->mouse_pos().y - (GImGui->LastItemData.Rect.Min.y + SCALE(elements->color_edit.circle_size) / 2)) / (GImGui->LastItemData.Rect.GetHeight() - SCALE(elements->color_edit.circle_size)));
                *value_changed = true;
            }

            xgui->easing(anim->grab[0], SCALE(elements->color_edit.circle_size) / 2 + anim->s * (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.circle_size)), 20.f, dynamic_easing);
            xgui->easing(anim->grab[1], SCALE(elements->color_edit.circle_size) / 2 + (1.f - anim->v) * (GImGui->LastItemData.Rect.GetHeight() - SCALE(elements->color_edit.circle_size)), 20.f, dynamic_easing);

            float R, G, B;
            ColorConvertHSVtoRGB(anim->h, 1.f, 1.f, R, G, B);

            draw->rect_filled_multi_color(xgui->window_drawlist(), GImGui->LastItemData.Rect.Min, GImGui->LastItemData.Rect.Max, draw->get_clr({ 1.f, 1.f, 1.f, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ 1.f, 1.f, 1.f, 1.f }), SCALE(elements->color_edit.content_rounding));
            draw->rect_filled_multi_color(xgui->window_drawlist(), GImGui->LastItemData.Rect.Min, GImGui->LastItemData.Rect.Max, draw->get_clr({ 0.f, 0.f, 0.f, 0.f }), draw->get_clr({ 0.f, 0.f, 0.f, 0.f }), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), SCALE(elements->color_edit.content_rounding - 1));

            draw->shadow_circle(xgui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(anim->grab[0], anim->grab[1]), SCALE(elements->color_edit.circle_size / 2), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), SCALE(15), ImVec2(0, 0), draw_flags_shadow_cut_out_shape_background);
            draw->circle(xgui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(anim->grab[0], anim->grab[1]), SCALE(elements->color_edit.circle_size / 2), draw->get_clr(clr->widgets.text), 30, SCALE(1));

            xgui->begin_group();
            {

                if (!alpha)
                    xgui->set_pos(xgui->get_pos().y + SCALE(elements->color_edit.button_height / 2 - (elements->color_edit.bar_size.y - elements->color_edit.grab_padding * 2) / 2), pos_y);

                xgui->invisible_button("hue_bar", SCALE(elements->color_edit.bar_size));

                if (xgui->is_item_active())
                {
                    anim->h = ImSaturate((xgui->mouse_pos().x - (GImGui->LastItemData.Rect.Min.x + SCALE(elements->color_edit.grab_width) / 2)) / (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.grab_width)));
                    *value_changed = true;
                }

                xgui->easing(anim->grab[2], SCALE(elements->color_edit.grab_width) / 2 + anim->h * (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.grab_width)), 20.f, dynamic_easing);

                for (int i = 0; i < IM_ARRAYSIZE(col_hues) - 1; ++i)
                    draw->rect_filled_multi_color(xgui->window_drawlist(), GImGui->LastItemData.Rect.Min + ImVec2(roundf(i * (GImGui->LastItemData.Rect.GetWidth() / 6)), SCALE(elements->color_edit.grab_padding)), ImVec2(GImGui->LastItemData.Rect.Min.x + roundf((i + 1) * (GImGui->LastItemData.Rect.GetWidth() / 6)), GImGui->LastItemData.Rect.Max.y - SCALE(elements->color_edit.grab_padding)), draw->get_clr(col_hues[i]), draw->get_clr(col_hues[i + 1]), draw->get_clr(col_hues[i + 1]), draw->get_clr(col_hues[i]), SCALE(10), i == 0 ? ImDrawFlags_RoundCornersLeft : i == 5 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone);

                draw->rect_filled(xgui->window_drawlist(), ImVec2(GImGui->LastItemData.Rect.Min.x + anim->grab[2] - SCALE(elements->color_edit.grab_width / 2), GImGui->LastItemData.Rect.Min.y), ImVec2(GImGui->LastItemData.Rect.Min.x + anim->grab[2] + SCALE(elements->color_edit.grab_width / 2), GImGui->LastItemData.Rect.Max.y), draw->get_clr(clr->widgets.text), SCALE(elements->color_edit.grab_width / 2));

                xgui->set_screen_pos(GImGui->LastItemData.Rect.Max.y + SCALE(14), pos_y);

                if (alpha)
                {
                    xgui->invisible_button("alpha_bar", SCALE(elements->color_edit.bar_size));

                    if (xgui->is_item_active())
                        color[3] = ImSaturate((xgui->mouse_pos().x - (GImGui->LastItemData.Rect.Min.x + SCALE(elements->color_edit.grab_width) / 2)) / (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.grab_width)));

                    xgui->easing(anim->grab[3], SCALE(elements->color_edit.grab_width) / 2 + color[3] * (GImGui->LastItemData.Rect.GetWidth() - SCALE(elements->color_edit.grab_width)), 20.f, dynamic_easing);

                    draw->rect_filled_multi_color(xgui->window_drawlist(), GImGui->LastItemData.Rect.Min + SCALE(0, elements->color_edit.grab_padding), GImGui->LastItemData.Rect.Max - SCALE(0, elements->color_edit.grab_padding), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ R, G, B, 1.f }), draw->get_clr({ 0.f, 0.f, 0.f, 1.f }), SCALE(elements->color_edit.rounding));

                    draw->rect_filled(xgui->window_drawlist(), ImVec2(GImGui->LastItemData.Rect.Min.x + anim->grab[3] - SCALE(elements->color_edit.grab_width / 2), GImGui->LastItemData.Rect.Min.y), ImVec2(GImGui->LastItemData.Rect.Min.x + anim->grab[3] + SCALE(elements->color_edit.grab_width / 2), GImGui->LastItemData.Rect.Max.y), draw->get_clr(clr->widgets.text), SCALE(elements->color_edit.grab_width / 2));
                }

            }
            xgui->end_group();

            xgui->sameline();

            if (picker_button("##picker", anim->pick_active).value_changed && !anim->pick_active)
                anim->pick_active = !anim->pick_active;

            if (anim->pick_active && xgui->mouse_clicked(mouse_button_left) && !ImRect(xgui->window_pos(), xgui->window_pos() + xgui->window_size()).Contains(xgui->mouse_pos()))
            {
                HDC hdcScreen = GetDC(NULL);
                COLORREF pick_color = GetPixel(hdcScreen, GetMousePos().x, GetMousePos().y);
                ReleaseDC(NULL, hdcScreen);

                color[0] = GetRValue(pick_color) / 255.0f;
                color[1] = GetGValue(pick_color) / 255.0f;
                color[2] = GetBValue(pick_color) / 255.0f;

                anim->pick_active = false;
            }

            draw->rect_filled(xgui->window_drawlist(), xgui->get_screen_pos(), anim->end_pos, draw->get_clr(clr->widgets.background), SCALE(elements->color_edit.content_rounding));
            draw->rect(xgui->window_drawlist(), xgui->get_screen_pos(), anim->end_pos, draw->get_clr(clr->widgets.stroke), SCALE(elements->color_edit.content_rounding), 0, SCALE(1));

            if (anim->type == 0)
            {
                char buf[4][5];
                for (int i = 0; i < 3 + alpha; ++i)
                {
                    snprintf(buf[i], sizeof(buf[i]), "%d", (int)(ImClamp(color[i] * (i < 3 ? 255.0f : 100.0f), 0.f, (i < 3 ? 255.0f : 100.0f))));

                    const char* label = (i == 0) ? "r_col" : (i == 1) ? "g_col" : (i == 2) ? "b_col" : "a_col";
                    if (widgets->picker_field(label, buf[i], sizeof(buf[i]), SCALE(alpha ? 32 : 43)).value_changed)
                    {
                        std::string str = buf[i];
                        if (!str.empty() && str.back() == '%')
                            str.pop_back();

                        if (!str.empty())
                            color[i] = std::stoi(str) / (i < 3 ? 255.0f : 100.0f);
                        else
                            color[i] = 0.f;
                    }

                    if (i < 3)
                    {
                        xgui->sameline(0, SCALE(1));
                        draw->rect_filled(xgui->window_drawlist(), ImVec2(GImGui->LastItemData.Rect.Max.x, GImGui->LastItemData.Rect.GetCenter().y - SCALE(4)), ImVec2(GImGui->LastItemData.Rect.Max.x + SCALE(1), GImGui->LastItemData.Rect.GetCenter().y + SCALE(4)), draw->get_clr(clr->widgets.stroke));
                    }
                }

                anim->end_pos = GImGui->LastItemData.Rect.Max;
            }
            else
            {
                if (input_hex("#HEX", color, alpha, SCALE(131)))
                    anim->update_hsv = true;
            }

            xgui->sameline();

            if (type_button(anim->type == 0 ? "RGB" : "HEX").value_changed)
                anim->type ^= 1;

        }
        xgui->end();
        xgui->pop_color(2);
        xgui->pop_var(5);
    }

    anim->h = ImClamp(anim->h, 0.001f, 0.999f);
    anim->s = ImClamp(anim->s, 0.001f, 0.999f);
    anim->v = ImClamp(anim->v, 0.001f, 0.999f);

    if (*value_changed)
    {
        xgui->hsv_to_rgb(anim->h, anim->s, anim->v, color[0], color[1], color[2]);
        *value_changed = false;
    }
}

static std::string build_color_title(const std::string& display_name, std::string_view key)
{
    std::string title = display_name;

    auto sep = title.find("##");
    if (sep != std::string::npos)
        title = title.substr(0, sep);

    const std::string enable_pfx = "Enable ";
    if (title.size() >= enable_pfx.size() && title.compare(0, enable_pfx.size(), enable_pfx) == 0)
        title = title.substr(enable_pfx.size());

    const std::string position_sfx = " Position";
    if (title.size() >= position_sfx.size() && title.compare(title.size() - position_sfx.size(), position_sfx.size(), position_sfx) == 0)
        title = title.substr(0, title.size() - position_sfx.size());

    // Se o nome já termina em " Color" (ex.: "Box Color", "Hidden Color"),
    // remove antes de anexar de novo — evita "Box Color Color".
    const std::string color_sfx = " Color";
    if (title.size() >= color_sfx.size() && title.compare(title.size() - color_sfx.size(), color_sfx.size(), color_sfx) == 0)
        title = title.substr(0, title.size() - color_sfx.size());

    if (key.find("##vis") != std::string_view::npos && key.find("##invis") == std::string_view::npos)
        title += " Visible Color";
    else if (key.find("##invis") != std::string_view::npos)
        title += " Invisible Color";
    else
        title += " Color";

    return title;
}

bool c_widgets::color_edit(std::string_view key)
{
    color_edit_t* data = cfg->fill<color_edit_t>(key.data());
    bool* value_changed = xgui->anim_container<bool>(xgui->get_window()->GetID((data->name.data() + std::string("_colorbox")).data()));

    ImRect swatch_rect = var->gui.color_edit_rect;

    std::string title = build_color_title(data->name, key);

    color_edit_ex(title, data->color.data(), data->alpha, var->gui.color_edit_rect, var->gui.color_edit_rect, value_changed);

    if (swatch_rect.Contains(xgui->mouse_pos()) && xgui->is_window_hovered())
    {
        const char* tooltip = nullptr;
        if (key.find("##vis")   != std::string_view::npos && key.find("##invis") == std::string_view::npos)
            tooltip = "Visible Color";
        else if (key.find("##invis") != std::string_view::npos)
            tooltip = "Invisible Color";

        if (tooltip)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, SCALE(1));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   SCALE(elements->child.rounding));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    SCALE(ImVec2(elements->child.padding.x, 6.0f)));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, draw->get_clr(clr->window.sub_background));
            ImGui::PushStyleColor(ImGuiCol_Border,  draw->get_clr(clr->window.stroke));

            ImGui::BeginTooltip();
            ImGui::PushFont(font->get(onest_medium_data, 14));
            ImGui::TextUnformatted(tooltip);
            ImGui::PopFont();
            ImGui::EndTooltip();

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
        }
    }

    var->gui.color_edit_rect = ImRect(var->gui.color_edit_rect.Min - SCALE(elements->checkbox.add_size * 2, 0), var->gui.color_edit_rect.GetBL() - SCALE(elements->checkbox.add_size, 0));

    return *value_changed;
}