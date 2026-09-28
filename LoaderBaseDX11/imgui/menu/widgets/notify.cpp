#include "../headers/functions.h"
#include "../headers/widgets.h"

// XISFPS port: FreeFire has no "notifications_pos" setting, so the notify
// popup lives in the fixed top-right corner.
static notify_position resolve_notify_position()
{
    return notify_pos_top_right;
}

void c_notify::add_notify(std::string_view text,  notify_type type)
{
    if (!enabled)
        return;

    // XISFPS port: FreeFire doesn't ship a "notifications_pos" setting yet,
    // so we always accept notifications (the popup itself is fixed to
    // notify_pos_top_right by resolve_notify_position above).

    notifications.push_back({ notify_count++, std::string(text), type });
}

void c_notify::setup_notify()
{
    if (!enabled)
        return;

    position = resolve_notify_position();

    int cur_notify_value = 0;
    float accumulated_height = 0.f;

    for (auto& notification : notifications)
    {
        cur_notify_value++;
        if (notification.active_notify)
            notification.notify_timer += xgui->fixed_speed(4.f);

        if (notification.notify_timer >= notify_time)
            notification.active_notify = false;

        xgui->easing(notification.notify_alpha, notification.active_notify ? 1.f : 0.f, 4.f, static_easing);

        if (notification.notify_alpha > 0.f)
        {
            float target_position = accumulated_height + notify_padding.y;
            xgui->easing(notification.notify_pos, target_position, 8.f, dynamic_easing);

            ImVec2 window_size = render_notify(cur_notify_value, notification.notify_alpha, notification.notify_timer, notification.notify_pos, notification.text, notification.type);

            accumulated_height += window_size.y + notify_spacing;
        }
    }
}

ImVec2 c_notify::render_notify(int cur_notify_value, float notify_alpha, float notify_percentage, float notify_pos, std::string_view text, notify_type type)
{
    ImVec2 window_size;

    ImGuiIO& io = ImGui::GetIO();

    ImVec2 pos;
    ImVec2 pivot{ 0.0f, 0.0f };

    switch (position)
    {
    case notify_pos_top_left:
        pos = ImVec2(notify_padding.x, notify_padding.y + notify_pos);
        pivot = ImVec2(0.0f, 0.0f);
        break;
    case notify_pos_top_center:
        pos = ImVec2(io.DisplaySize.x * 0.5f, notify_padding.y + notify_pos);
        pivot = ImVec2(0.5f, 0.0f);
        break;
    case notify_pos_top_right:
        pos = ImVec2(io.DisplaySize.x - notify_padding.x, notify_padding.y + notify_pos);
        pivot = ImVec2(1.0f, 0.0f);
        break;
    case notify_pos_center_left:
        pos = ImVec2(notify_padding.x, io.DisplaySize.y * 0.5f + notify_pos);
        pivot = ImVec2(0.0f, 0.5f);
        break;
    case notify_pos_center:
        pos = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f + notify_pos);
        pivot = ImVec2(0.5f, 0.5f);
        break;
    case notify_pos_center_right:
        pos = ImVec2(io.DisplaySize.x - notify_padding.x, io.DisplaySize.y * 0.5f + notify_pos);
        pivot = ImVec2(1.0f, 0.5f);
        break;
    case notify_pos_bottom_left:
        pos = ImVec2(notify_padding.x, io.DisplaySize.y - notify_padding.y - notify_pos);
        pivot = ImVec2(0.0f, 1.0f);
        break;
    case notify_pos_bottom_center:
        pos = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - notify_padding.y - notify_pos);
        pivot = ImVec2(0.5f, 1.0f);
        break;
    case notify_pos_bottom_right:
        pos = ImVec2(io.DisplaySize.x - notify_padding.x, io.DisplaySize.y - notify_padding.y - notify_pos);
        pivot = ImVec2(1.0f, 1.0f);
        break;
    default:
        pos = ImVec2(notify_padding.x, notify_padding.y + notify_pos);
        pivot = ImVec2(0.0f, 0.0f);
        break;
    }

    SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    xgui->push_var(ImGuiStyleVar_Alpha, notify_alpha);
    xgui->begin("notify_" + std::to_string(cur_notify_value), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_AlwaysAutoResize);
    {
        ImDrawList* dl = xgui->window_drawlist();
        ImFont* f = font->get(onest_medium_data, 14);

        const float padding_x = 14.f;
        const float padding_y = 6.f;
        const float bar_height = 3.f;

        ImVec2 text_size = xgui->text_size(f, text.data());
        ImVec2 box_size = ImVec2(text_size.x + padding_x * 2.0f, text_size.y + padding_y * 2.0f + bar_height + 2.0f);

        xgui->dummy(box_size);

        ImVec2 box_min = xgui->window_pos();
        ImVec2 box_max = box_min + box_size;

        draw->rect_filled(dl, box_min, box_max, draw->get_clr(clr->window.sub_background), 6.f);

        ImVec4 type_clr;
        switch (type)
        {
        case warning: type_clr = ImVec4(1.00f, 0.72f, 0.18f, 1.0f); break;
        case error:   type_clr = ImVec4(1.00f, 0.35f, 0.35f, 1.0f); break;
        case success:
        default:      type_clr = ImVec4(0.36f, 0.92f, 0.46f, 1.0f); break;
        }

        ImVec2 text_min = box_min + ImVec2(padding_x, padding_y);
        ImVec2 text_max = ImVec2(box_max.x - padding_x, box_max.y - padding_y - bar_height - 2.0f);
        draw->text_clipped(dl, f, text_min, text_max, draw->get_clr(clr->widgets.text), text.data(), nullptr, nullptr, ImVec2(0.f, 0.5f));

        float t = notify_time > 0.0f ? (notify_percentage / notify_time) : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float remaining = 1.0f - t;

        if (remaining > 0.0f)
        {
            float full_width = box_size.x - padding_x * 2.0f;
            float bar_width = full_width * remaining;

            float offset = full_width * t;
            ImVec2 bar_max = ImVec2(box_max.x - padding_x - offset, box_max.y - padding_y);
            ImVec2 bar_min = ImVec2(bar_max.x - bar_width, bar_max.y - bar_height);

            draw->rect_filled(dl, bar_min, bar_max, draw->get_clr(type_clr), bar_height / 2.0f);
        }

        window_size = box_size;
    }
    xgui->end();
    xgui->pop_var();

    return window_size;
}