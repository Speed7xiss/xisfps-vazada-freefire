#pragma once
#include "imgui.h"
#include <memory>

//
// XISFPS palette — Neutral Gray/White, ported from the user's other cheat
// project (LoaderBaseDX11).
//
// The other project defines the "brand" in three tones — the widget slab is
// near-black, active fills are medium grey, and only the actively-checked
// glyph is white. There is no colored accent; everything works in greyscale.
// Comments in the source of that project literally read "Neutral Gray/White
// palette", which is what we replicate here.
//
// Reference values ported from that project:
//   Base fill / accent  = ImColor(80,  80,  80)  → #505050
//   SecondaryText       = ImColor(50,  50,  50)  → #323232
//   Widget background   = IM_COL32(22, 22, 23)   → #161617
//   Slider grab         = IM_COL32(200,200,200)  → #C8C8C8
//   Body / darkest fill = ImVec4(0.086,0.086,0.090) ≈ #16161717
//
class c_colors
{
public:
    // "Accent" in XISFPS = the medium grey used by the other project for
    // active toggles. Kept configurable so the user can still recolor at
    // runtime (Menu Accent Color), but the shipped default is greyscale.
    ImColor accent{ 80, 80, 80 };

    struct
    {
        // Ported directly from the reference project — measured on-screen:
        //   Base WindowBg RGB(13,13,14), inner slab RGB(15,15,16).
        ImColor background     { 13, 13, 14 };   // main content pane body
        ImColor sub_background { 15, 15, 16 };   // sidebar / topbar / cards
        ImColor stroke         { 30, 30, 34 };   // panel separators
        ImColor section        { 22, 22, 24 };   // active sidebar-row pill fill
    } window;

    struct
    {
        ImColor background     { 22, 22, 23 };   // frame bg — matches FrameBg from custom.hpp
        ImColor sub_background { 28, 28, 32 };   // hover / popup fill (ComboCustom FrameBgActive)
        ImColor text           { 235, 235, 238 };
        ImColor text_inactive  { 80,  80,  80 }; // ported "SecundaryText"
        ImColor stroke         { 40, 40, 45 };   // ComboCustom Border
        ImColor sub_stroke     { 60, 60, 63 };
    } widgets;
};

inline std::unique_ptr<c_colors> clr = std::make_unique<c_colors>();
