#pragma once
#include <map>
#include <variant>
#include <array>
#include <vector>
#include <string>
#include <mutex>
#include <memory>

struct key_data_t
{
    int key;
    int mode;
};

struct checkbox_t
{
    std::string name;
    bool callback;
    bool keybind;
    key_data_t key_data;
    bool stored_value;
    bool is_active = false;
};

template <typename T>
struct slider_data
{
    std::string name;
    T callback;
    T min;
    T max;
    std::string format;
};

template <typename T>
struct range_data
{
    std::string name;
    T callback;
    T callback_two;
    T min;
    T max;
    T range;
    std::string format;
};

template <typename T>
struct dropdown_data
{
    std::string name;
    T callback;
    std::vector<std::string> items;
};

struct color_edit_t
{
    std::string name;
    std::array<float, 4> color;
    bool alpha;
};

enum config_type
{
    checkbox_type,
    slider_int_type,
    slider_float_type,
    range_int_type,
    range_float_type,
    dropdown_type,
    multi_dropdown_type,
    color_edit_type
};

using string_t = std::vector<std::string>;
using bool_t = std::vector<bool>;
using col_t = std::array<float, 4>;

using slider_int_t = slider_data<int>;
using slider_float_t = slider_data<float>;
using range_int_t = range_data<int>;
using range_float_t = range_data<float>;
using dropdown_t = dropdown_data<int>;
using multi_dropdown_t = dropdown_data<bool_t>;
using config_variant = std::variant<checkbox_t, slider_int_t, slider_float_t, range_int_t, range_float_t, dropdown_t, multi_dropdown_t, color_edit_t>;

class c_config
{
public:

    void init_config();

    template <typename T>
    T& get(const std::string& name) { return std::get<T>(options[name]); }

    template <typename T>
    T* fill(const std::string& name)
    {
        auto& option = options[name];

        return std::get_if<T>(&option);
    }

    void process_keybinds();

    struct action_keybind_t
    {
        std::string config_key;
        void      (*fire)();
    };

    std::vector<action_keybind_t> action_keybinds;

    void register_action_keybind(const std::string& key, void (*fn)())
    {
        action_keybinds.push_back({ key, fn });
    }

    std::vector<std::pair<std::string, int>> order;

    std::map<std::string, config_variant>& all_options() { return options; }

private:

    template <typename, typename = void>
    struct has_keybind : std::false_type {};

    template <typename T>
    struct has_keybind<T, std::void_t<decltype(std::declval<T>().keybind)>> : std::true_type {};

    template <typename T, typename... Args>
    void add_option(const std::string& name, Args&&... args)
    {
        T option{ name, std::forward<Args>(args)... };
        options[name] = option;
        order.push_back({ name, get_type<T>() });
    }

    template <typename T>
    int get_type() const
    {
        if constexpr (std::is_same_v<T, checkbox_t>) return checkbox_type;
        if constexpr (std::is_same_v<T, slider_int_t>) return slider_int_type;
        if constexpr (std::is_same_v<T, slider_float_t>) return slider_float_type;
        if constexpr (std::is_same_v<T, range_int_t>) return range_int_type;
        if constexpr (std::is_same_v<T, range_float_t>) return range_float_type;
        if constexpr (std::is_same_v<T, dropdown_t>) return dropdown_type;
        if constexpr (std::is_same_v<T, multi_dropdown_t>) return multi_dropdown_type;
        if constexpr (std::is_same_v<T, color_edit_t>) return color_edit_type;
    }

    std::map<std::string, config_variant> options;
};

inline std::unique_ptr<c_config> cfg = std::make_unique<c_config>();
