#pragma once
#include "includes.h"
#include "../headers/config.h"
#include <functional>

#define IMGUI_DEFINE_MATH_OPERATORS

struct widget_t
{
    ImGuiID id;
    bool value_changed;
    ImRect rect;
    ImVec2 size;
    bool visible;
    bool hovered;

    widget_t* operator->() {
        return this;
    }

    const widget_t* operator->() const {
        return this;
    }
};

class c_widgets
{
public:

    widget_t section(std::string_view icon, int index, int& count);
    // XISFPS overload: textual sidebar row (uses `name` as the visible label).
    widget_t section(std::string_view icon, std::string_view name, int index, int& count);

    void begin_section(std::string_view name);

    void end_section();

    bool begin_sub_section(std::string_view name);

    void end_sub_section();

    widget_t sub_section_list(std::string_view name, int index, int& count);

    widget_t sub_section(std::string_view name, int index, int& count, bool lua_section = false);

    void begin_child(std::string_view name);
    // XISFPS: overload that paints a big magenta title inside the card,
    // as in the reference UI ("Aim Assistance", "Aim Settings", …).
    void begin_child(std::string_view name, std::string_view title);

    void end_child();

    widget_t checkbox(std::string_view key);

    widget_t checkbox(std::string_view name, bool* value);

    widget_t checkbox(std::string_view key, std::function<void()> extra_in_cog);

    widget_t slider_int(std::string_view key);

    widget_t slider_float(std::string_view key);

    widget_t range_int(std::string_view key);

    widget_t range_float(std::string_view key);

    widget_t dropdown(std::string_view key);

    widget_t multi_dropdown(std::string_view key);

    widget_t button(std::string_view name);

    widget_t button_keybind(std::string_view label, std::string_view bind_key);

    widget_t slider_float_raw(std::string_view name, float* value, float min, float max, std::string_view format, float width = 0.f);

    widget_t key_select(std::string_view name, int* key);

    widget_t mode_select(std::string_view name, int* mode);

    bool color_edit(std::string_view key);

    bool color_position(std::string_view key);

    bool color_position_dual(std::string_view key, std::string_view extra_checkbox_key = {});

    bool slider_float_cog(std::string_view key);

    bool cog(std::string_view id, std::function<void()> body);

    bool dropdown_cog(std::string_view key);

    bool box_style_cog(std::string_view dropdown_key, std::string_view color_key);

    bool flag_admin_cog(std::string_view snapline_key, std::string_view snapline_color_key);

    bool head_circle_cog(std::string_view style_key, std::string_view size_key, std::string_view thickness_key);

    widget_t picker_field(std::string_view name, char* buf, int size, float width);

    widget_t hex_field(std::string_view name, char* buf, int size, float width);

    widget_t search_field(std::string_view name, char* buf, int size, float width);

    widget_t list_search_field(std::string_view name, char* buf, int size, float width);

    widget_t input_field(std::string_view name, char* buf, int size);
};

inline std::unique_ptr<c_widgets> widgets = std::make_unique<c_widgets>();

enum notify_type
{
    success = 0,
    warning = 1,
    error = 2
};

enum notify_position
{
    notify_pos_top_left = 0,
    notify_pos_top_center,
    notify_pos_top_right,
    notify_pos_center_left,
    notify_pos_center,
    notify_pos_center_right,
    notify_pos_bottom_left,
    notify_pos_bottom_center,
    notify_pos_bottom_right
};

struct notify_state
{
    int notify_id;
    std::string text;
    notify_type type{ success };

    ImVec2 window_size{ 0, 0 };
    float notify_alpha{ 0 };
    bool active_notify{ true };
    float notify_timer{ 0 };
    float notify_pos{ 0 };
};

class c_notify
{
public:
    void setup_notify();

    void add_notify(std::string_view text, notify_type type);

    void set_enabled(bool v) { enabled = v; }
    bool is_enabled() const { return enabled; }

    void set_position(notify_position pos) { position = pos; }
    notify_position get_position() const { return position; }

private:
    ImVec2 render_notify(int cur_notify_value, float notify_alpha, float notify_percentage, float notify_pos, std::string_view text, notify_type type);

    float notify_time{ 15 };
    int notify_count{ 0 };

    float notify_spacing{ 20 };
    ImVec2 notify_padding{ 20, 20 };

    std::vector<notify_state> notifications;

    bool enabled{ true };
    notify_position position{ notify_pos_top_right };

};

inline std::unique_ptr<c_notify> xnotify = std::make_unique<c_notify>();
