#include "../headers/includes.h"
#include "imgui_impl_dx11.h"

void c_font::update()
{
    if (var->gui.dpi_changed && !data.empty())
    {
        var->gui.dpi = var->gui.stored_dpi / 100.f;

        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;

        ImGuiIO& io = ImGui::GetIO();

        bool added = false;

        static const ImWchar custom_ranges[] = {
            0x0020, 0x00FF,
            0x0400, 0x052F,
            0x2DE0, 0x2DFF,
            0xA640, 0xA69F,
            0x2010, 0x205E,
            0x2600, 0x26FF,
            0x2700, 0x27BF,
            0,
        };

        for (auto& font_t : data)
        {
            if (!font_t.font)
            {
                font_t.font = io.Fonts->AddFontFromMemoryTTF(font_t.data.data(), font_t.data.size(), SCALE(font_t.size), &cfg, custom_ranges);
                added = true;
            }
        }

        if (added)
        {
            io.Fonts->Build();
            ImGui_ImplDX11_CreateDeviceObjects();
        }

        var->gui.dpi_changed = false;
    }
}

ImFont* c_font::get(std::vector<unsigned char> font_data, float size)
{
    for (auto& font : data)
    {
        if (font.data == font_data && font.size == size)
        {
            return font.font;
        }
    }

    add(font_data, size);

    var->gui.dpi_changed = true;

    return get(font_data, size);
}

void c_font::add(std::vector<unsigned char> font_data, float size)
{
    data.push_back({ font_data, size, nullptr });
}
