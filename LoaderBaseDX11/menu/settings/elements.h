#pragma once
#include <string>
#include "imgui.h"

//
// XISFPS element metrics.
//
// Rounding was pushed from 4 to 6-8 across the widget family to give the UI
// a more contemporary, slightly softer look without going full "pill". The
// checkbox is now a proper toggle capsule; the sidebar section pill is 8px
// so its left-accent bar reads cleanly at any DPI.
//
class c_elements
{
public:

    struct
    {
        float window_padding{ 15 };
        float line_padding{ 25 };
        float text_padding{ 10 };
        float rounding{ 4 };
        float image_size{ 30 };
    } topbar;

    struct
    {
        ImVec2 padding{ 10, 10 };
        ImVec2 spacing{ 10, 10 };
        // Row highlight in the MultiLoader reads as ~4 px; keeping in sync.
        float rounding{ 4 };
    } section;

    struct
    {
        ImVec2 padding{ 15, 15 };
        ImVec2 spacing{ 15, 20 };
        float rounding{ 4 };
        float top{ 25 };
        float bottom{ 25 };
        float item_spacing{ 17 };
    } sub_section;

    struct
    {
        // XISFPS: 18 px top padding so the titled cards sit visibly below
        // the header's magenta baseline rather than touching it.
        ImVec2 padding{ 14, 18 };
        ImVec2 spacing{ 12, 12 };
    } content;

    struct
    {
        ImVec2 padding{ 15, 15 };
        ImVec2 spacing{ 15, 15 };
        // XISFPS: matches the MultiLoader's Custom::CustomChild rounding
        // (8.0f) — the outer window stays near-square (1) but each panel
        // reads as a distinct rounded card, exactly like the loader's
        // "SELECT YOUR LOADER" / "INFO" panels.
        float rounding{ 8 };
    } child;

    struct
    {
        float height{ 15 };
        float size{ 26 };               // was 25 — matches new capsule feel
        float radius{ 5.f };            // was 4.5 — meatier knob
        float padding{ 3 };
        float add_size{ 15 };
    } checkbox;

    struct
    {
        float height{ 25 };
        float size{ 11 };               // was 9 — chunkier knob
        float padding{ 1 };
    } slider;

    struct
    {
        float height{ 48 };
        float size{ 30 };
        float padding{ 10 };
        float rounding{ 3 };            // FrameRounding in the MultiLoader
        float window_padding{ 10 };
        float scrollbar_content{ 8 };
        ImVec2 scrollbar_border{ 8, 8 };
    } dropdown;

    struct
    {
        float height{ 30 };
        float padding{ 10 };
    } selectable;

    struct
    {
        float height{ 30 };
        float rounding{ 5 };            // MultiLoader Custom::Button rounding
    } button;

    struct
    {
        float height{ 30 };
        float rounding{ 5 };
        float width{ 170 };
        float window_padding{ 10 };
        ImVec2 padding{ 10, 10 };
        ImVec2 spacing{ 10, 10 };
    } keybind;

    struct
    {
        // Popup do color picker — encolhido para caber num menu compacto
        // como o do FreeFire (janela 720x460). Antes: 220 largura, 35 título,
        // pad/spacing 10 — ficava desproporcional ao Card 300px de largura.
        float width{ 170 };
        float title{ 24 };
        float rounding{ 6 };
        float content_rounding{ 5 };
        float circle_size{ 8 };
        ImVec2 padding{ 6, 6 };
        ImVec2 spacing{ 6, 6 };
        ImVec2 bar_size{ 118, 6 };
        float grab_width{ 4 };
        float grab_padding{ 1 };
        float button_height{ 22 };
        float window_padding{ 6 };
    } color_edit;

    struct
    {
        float height{ 52 };
        float size{ 30 };
        float rounding{ 6 };            // was 2 — matches dropdown/button
        float padding{ 12 };
        float line_padding{ 7 };
        float search_rounding{ 6 };     // was 4
    } text_field;
};

inline std::unique_ptr<c_elements> elements = std::make_unique<c_elements>();
