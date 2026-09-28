#include "../headers/includes.h"
#include "../headers/widgets.h"
#include <algorithm>

void c_search::search()
{
    ImGuiContext& g = *GImGui;
    const ImVec2 pos = GetWindowPos();
    const ImVec2 size = GetWindowSize();
    ImDrawList* drawlist = GetWindowDrawList();

    xgui->easing(window_alpha, active_searching ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(text_field_width, active_searching ? window_size.x : 170, 16.f, dynamic_easing);

    if (window_alpha < 0.01f)
        return;

    xgui->push_var(style_var_alpha, window_alpha);

    xgui->set_pos(ImVec2(0, 0), pos_all);
    xgui->begin_content("dimming window", size);
    {

        draw->text_clipped(xgui->background_drawlist(), font->get(onest_medium_data, 20), ImVec2(0, 0), ImVec2(1920, 1080), ImColor(255, 255, 255), std::to_string(window_hovered).data());
        if (!window_hovered && xgui->mouse_clicked(mouse_button_left))
            active_searching = false;

        draw->rect_filled(xgui->window_drawlist(), xgui->window_pos(), xgui->window_pos() + xgui->window_size(), draw->get_clr(clr->window.background, 0.8f), var->style.window_rounding);

        xgui->push_color(style_col_child_bg, draw->get_clr(clr->window.sub_background));
        xgui->push_color(style_col_border, draw->get_clr(clr->window.stroke));
        xgui->push_var(style_var_child_border_size, SCALE(1));
        xgui->push_var(style_var_child_rounding, SCALE(elements->child.rounding));
        // XISFPS: Y offset switched from bar_width to header_height. The
        // search UI is currently gated off (early-out above) but keeping this
        // consistent means the overlay lines up correctly if it's re-enabled.
        xgui->set_pos(SCALE(var->gui.bar_width + elements->sub_section.padding.x, var->gui.header_height), pos_all);
        xgui->begin_content("search content", SCALE(window_size), SCALE(elements->child.padding), SCALE(elements->child.spacing), window_flags_no_move);
        {
            draw->rect(xgui->window_drawlist(), xgui->window_pos(), xgui->window_pos() + xgui->window_size(), draw->get_clr(clr->window.stroke), SCALE(elements->child.rounding), 0, SCALE(1));
            window_hovered = ImRect{xgui->window_pos(), xgui->window_pos() + xgui->window_size()}.Contains(xgui->mouse_pos()) || (GImGui->HoveredWindow ? (strstr(GImGui->HoveredWindow->Name, "dropdown_window") || strstr(GImGui->HoveredWindow->Name, "coloredit_window") || strstr(GImGui->HoveredWindow->Name, "keybind_window")) : false);
            std::string search_element_lower = search_element;
            std::transform(search_element_lower.begin(), search_element_lower.end(), search_element_lower.begin(), [](unsigned char c) { return std::tolower(c); });

            for (int i = 0; i < cfg->order.size(); i++)
            {
                const std::string& full_name = cfg->order.at(i).first;

                if (full_name.find("##") != std::string::npos)
                    continue;

                std::string name_lower = full_name;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), [](unsigned char c) { return std::tolower(c); });

                if (search_element.empty())
                    continue;

                if (!search_element_lower.empty() && name_lower.find(search_element_lower) == std::string::npos)
                    continue;

                if (cfg->order.at(i).second == checkbox_type)
                {
                    widgets->checkbox(cfg->order.at(i).first.data());
                }
                if (cfg->order.at(i).second == slider_int_type)
                {
                    widgets->slider_int(cfg->order.at(i).first.data());
                }
                if (cfg->order.at(i).second == slider_float_type)
                {
                    widgets->slider_float(cfg->order.at(i).first.data());
                }
                if (cfg->order.at(i).second == range_int_type)
                {
                    widgets->range_int(cfg->order.at(i).first.data());
                }
                if (cfg->order.at(i).second == range_float_type)
                {
                    widgets->range_float(cfg->order.at(i).first.data());
                }
                if (cfg->order.at(i).second == dropdown_type)
                {
                    widgets->dropdown(cfg->order.at(i).first.data());
                }
                if (cfg->order.at(i).second == multi_dropdown_type)
                {
                    widgets->multi_dropdown(cfg->order.at(i).first.data());
                }
                if (cfg->order.at(i).second == color_edit_type)
                {
                    widgets->color_edit(cfg->order.at(i).first.data());
                }
            }
        }
        xgui->end_content();
        xgui->pop_var(2);
        xgui->pop_color(2);
    }
    xgui->end_content();
    xgui->pop_var();
}
