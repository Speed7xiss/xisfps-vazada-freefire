// =====================================================================
// SkinChanger tab — redesigned layout (v2).
//
// Layout:
//   Two children side by side — no top bar, no chips row.
//
//   LEFT CHILD (fixed 160 px):
//     - Category list (top→bottom: Chapéu, Maquiagem, Máscara,
//                      Jaqueta, Calça, Tênis, Presets)
//     - "Clean" button at the very bottom
//
//   RIGHT CHILD (remainder):
//     - Top row: styled search field (left) + rarity combobox (right)
//     - Skin list below (virtualized, 1-click apply, themed hover)
//
// 1-click smart apply: clicking a skin applies it as the wildcard for
// the currently selected category. You can't accidentally put a shirt
// in the pants slot — the category tab is your scope.
// =====================================================================
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "../../../Cheat/SkinChanger/SkinChanger.hpp"

#include "../headers/includes.h"

// Forward-declare the three free functions that live in dropdown.cpp.
// Including the full header would pull in xgui internals we don't need here.
bool     begin_dropdown(std::string_view name, std::string& preview, int val, widget_t& data, bool multi = false);
widget_t selectable(std::string_view name, bool active);
void     end_dropdown();

namespace sections { namespace skinchanger {

namespace detail {

// ---- Category definitions (display order top → bottom) -----------------

struct CatDef {
    SkinChanger::Category cat;
    const char*           label;
};

// Category names in PT-BR, shorter than before.
// Encoding: UTF-8 byte-by-byte so MSVC doesn't choke on \xNN sequences.
inline constexpr CatDef kCatDefs[] = {
    { SkinChanger::Category::Head,      "Chap\xc3\xa9u"  },  // Chapéu
    { SkinChanger::Category::Facepaint, "Maquiagem"       },
    { SkinChanger::Category::Mask,      "M\xc3\xa1scara"  },  // Máscara
    { SkinChanger::Category::Top,       "Jaqueta"          },
    { SkinChanger::Category::Bottom,    "Cal\xc3\xa7""a"  },  // Calça
    { SkinChanger::Category::Shoes,     "T\xc3\xaanis"    },  // Tênis
};
inline constexpr int kCatDefCount = 6;
inline constexpr int kPresetsIdx  = 6;  // virtual "Presets" tab index

// ---- Rarity filter labels -----------------------------------------------

inline constexpr int kRarityFilterN = 8;
inline constexpr const char* kRarityFilterLabels[kRarityFilterN] = {
    "Todas",
    "Comum",
    "Incomum",
    "Rara",
    "\xc3\x89pica",       // Épica
    "Lend\xc3\xa1ria",    // Lendária
    "M\xc3\xadtica+",     // Mítica+
    "Vermelha",
};

// ---- Rarity dot colour --------------------------------------------------

inline ImU32 rarity_color(SkinChanger::Rarity r) {
    switch (r) {
    case SkinChanger::Rarity::GREEN:       return IM_COL32(112, 220,  96, 255);
    case SkinChanger::Rarity::BLUE:        return IM_COL32( 82, 174, 240, 255);
    case SkinChanger::Rarity::PURPLE:      return IM_COL32(178, 120, 240, 255);
    case SkinChanger::Rarity::ORANGE:      return IM_COL32(255, 160,  56, 255);
    case SkinChanger::Rarity::ORANGE_PLUS: return IM_COL32(255, 120,   0, 255);
    case SkinChanger::Rarity::RED:         return IM_COL32(240,  90,  90, 255);
    case SkinChanger::Rarity::WHITE:
    default:                               return IM_COL32(200, 200, 200, 255);
    }
}

// ---- Persistent UI state ------------------------------------------------

struct ui_state {
    int  cat_tab    = 0;
    char search[64] = {};
    int  rarity_flt = 0;
    std::string rarity_preview{ "Todas" };

    std::vector<const SkinChanger::ClothDbEntry*> filtered;
    int         cache_cat    = -1;
    std::string cache_search;
    int         cache_rarity = -1;
};

inline ui_state& g_ui() { static ui_state s; return s; }

// ---- Filter logic -------------------------------------------------------

inline void refresh_filter(ui_state& s)
{
    const bool same = s.cache_cat    == s.cat_tab
                   && s.cache_search == s.search
                   && s.cache_rarity == s.rarity_flt;
    if (same && !s.filtered.empty()) return;

    s.filtered.clear();
    if (s.cat_tab < 0 || s.cat_tab >= kCatDefCount) return;

    const auto& src = SkinChanger::EntriesInCategory(kCatDefs[s.cat_tab].cat);
    s.filtered.reserve(src.size());

    const int want_rarity = s.rarity_flt - 1;   // -1 = any
    for (auto* e : src) {
        if (want_rarity >= 0 && (int)e->rarity != want_rarity) continue;
        s.filtered.push_back(e);
    }

    if (s.search[0] != '\0') {
        std::string needle = s.search;
        std::transform(needle.begin(), needle.end(), needle.begin(),
            [](unsigned char c){ return (char)std::tolower(c); });
        uint32_t num    = 0;
        bool     is_num = !needle.empty();
        for (unsigned char c : needle) {
            if (c < '0' || c > '9') { is_num = false; break; }
            num = num * 10 + (c - '0');
        }
        s.filtered.erase(
            std::remove_if(s.filtered.begin(), s.filtered.end(),
                [&](const SkinChanger::ClothDbEntry* e) {
                    if (is_num && e->id == num) return false;
                    std::string desc = e->description ? e->description : "";
                    std::transform(desc.begin(), desc.end(), desc.begin(),
                        [](unsigned char c){ return (char)std::tolower(c); });
                    return desc.find(needle) == std::string::npos;
                }),
            s.filtered.end());
    }

    s.cache_cat    = s.cat_tab;
    s.cache_search = s.search;
    s.cache_rarity = s.rarity_flt;
}

// ---- Get active skin ID for current category ---------------------------

inline uint32_t active_id_for_cat(int cat_tab)
{
    if (cat_tab < 0 || cat_tab >= kCatDefCount) return 0;
    const SkinChanger::Category c = kCatDefs[cat_tab].cat;
    auto wildcards = SkinChanger::Wildcards();
    for (const auto& w : wildcards) if (w.category == c) return w.targetId;
    return 0;
}

// ---- Draw: left child (categories + Clean) -----------------------------

inline void draw_left_child(int& cat_tab, float col_w, float col_h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin  = ImGui::GetCursorScreenPos();
    const float rnd = SCALE(elements->child.rounding);

    dl->AddRectFilled(origin, ImVec2(origin.x + col_w, origin.y + col_h),
        draw->get_clr(clr->window.sub_background), rnd);
    dl->AddRect      (origin, ImVec2(origin.x + col_w, origin.y + col_h),
        draw->get_clr(clr->window.stroke),         rnd, 0, SCALE(1.f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, SCALE(elements->child.padding));
    ImGui::BeginChild("##sc_cats", ImVec2(col_w, col_h), false,
        ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImFont* fnt = font->get(onest_medium_data, 13);

    auto wildcards = SkinChanger::Wildcards();
    auto is_active = [&](SkinChanger::Category c) {
        for (const auto& w : wildcards) if (w.category == c) return true;
        return false;
    };

    // ---- Real categories ------------------------------------------------
    for (int i = 0; i < kCatDefCount; ++i) {
        const bool selected = (cat_tab == i);
        const bool active   = is_active(kCatDefs[i].cat);
        const float tab_w   = ImGui::GetContentRegionAvail().x;
        const float tab_h   = SCALE(30.f);
        const ImVec2 tp     = ImGui::GetCursorScreenPos();
        const ImVec2 tmax   = ImVec2(tp.x + tab_w, tp.y + tab_h);

        char id[24]; std::snprintf(id, sizeof(id), "##sc_cat_%d", i);
        ImGui::InvisibleButton(id, ImVec2(tab_w, tab_h));
        if (ImGui::IsItemClicked()) cat_tab = i;

        ImU32 bg = IM_COL32(0, 0, 0, 0);
        if (selected)                    bg = IM_COL32(45, 45, 48, 220);
        else if (ImGui::IsItemHovered()) bg = IM_COL32(32, 32, 35, 180);
        dl->AddRectFilled(tp, tmax, bg, SCALE(4.f));

        // Left accent bar — lights up when this category has a wildcard.
        if (active) {
            dl->AddRectFilled(
                ImVec2(tp.x + SCALE(2.f), tp.y + SCALE(7.f)),
                ImVec2(tp.x + SCALE(4.f), tmax.y - SCALE(7.f)),
                draw->get_clr(clr->accent), SCALE(2.f));
        }

        draw->text_clipped(dl, fnt,
            ImVec2(tp.x + SCALE(12.f), tp.y),
            ImVec2(tmax.x - SCALE(4.f),  tmax.y),
            selected ? draw->get_clr(clr->widgets.text)
                     : draw->get_clr(clr->widgets.text_inactive),
            kCatDefs[i].label, nullptr, nullptr, ImVec2(0.f, 0.5f));

        ImGui::Dummy(ImVec2(0.f, SCALE(2.f)));
    }

    // ---- Presets tab (placeholder) -------------------------------------
    {
        const int i = kPresetsIdx;
        const bool selected = (cat_tab == i);
        const float tab_w   = ImGui::GetContentRegionAvail().x;
        const float tab_h   = SCALE(30.f);
        const ImVec2 tp     = ImGui::GetCursorScreenPos();
        const ImVec2 tmax   = ImVec2(tp.x + tab_w, tp.y + tab_h);

        ImGui::InvisibleButton("##sc_cat_presets", ImVec2(tab_w, tab_h));
        if (ImGui::IsItemClicked()) cat_tab = i;

        ImU32 bg = IM_COL32(0, 0, 0, 0);
        if (selected)                    bg = IM_COL32(45, 45, 48, 220);
        else if (ImGui::IsItemHovered()) bg = IM_COL32(32, 32, 35, 180);
        dl->AddRectFilled(tp, tmax, bg, SCALE(4.f));

        draw->text_clipped(dl, fnt,
            ImVec2(tp.x + SCALE(12.f), tp.y),
            ImVec2(tmax.x - SCALE(4.f),  tmax.y),
            selected ? draw->get_clr(clr->widgets.text)
                     : draw->get_clr(clr->widgets.text_inactive),
            "Presets", nullptr, nullptr, ImVec2(0.f, 0.5f));
        ImGui::Dummy(ImVec2(0.f, SCALE(2.f)));
    }

    // ---- Clean button — pinned to the bottom of the left child. -----------
    // Uses the project's styled button (same as "Reload") via widgets->button.
    const float btn_h   = SCALE(elements->button.height);
    const float pad_bot = SCALE(elements->child.padding.y);
    const float space   = ImGui::GetContentRegionAvail().y - btn_h - pad_bot;
    if (space > 0.f) ImGui::Dummy(ImVec2(0.f, space));

    if (widgets->button("Clean").value_changed)
        SkinChanger::ClearAll();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---- Draw: right child (search + rarity + list) ------------------------

inline void draw_right_child(ui_state& s, float col_w, float col_h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin  = ImGui::GetCursorScreenPos();
    const float rnd = SCALE(elements->child.rounding);

    dl->AddRectFilled(origin, ImVec2(origin.x + col_w, origin.y + col_h),
        draw->get_clr(clr->window.sub_background), rnd);
    dl->AddRect      (origin, ImVec2(origin.x + col_w, origin.y + col_h),
        draw->get_clr(clr->window.stroke),         rnd, 0, SCALE(1.f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, SCALE(elements->child.padding));
    ImGui::BeginChild("##sc_right", ImVec2(col_w, col_h), false,
        ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoMove);

    // ---- Presets placeholder ------------------------------------------
    if (s.cat_tab == kPresetsIdx) {
        ImFont* fnt = font->get(onest_medium_data, 13);
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const float  rw = ImGui::GetContentRegionAvail().x;
        draw->text_clipped(dl, fnt, cp, ImVec2(cp.x + rw, cp.y + SCALE(20.f)),
            draw->get_clr(clr->widgets.text_inactive),
            "Presets — em breve.", nullptr, nullptr, ImVec2(0.f, 0.5f));
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        return;
    }

    // ---- Search field (full width) -----------------------------------
    const float full_w = ImGui::GetContentRegionAvail().x;
    widgets->list_search_field("Busque a Skin desejada",
        s.search, sizeof(s.search), full_w);

    // ---- Rarity dropdown — project-styled, identical to ESP combos ---
    // `begin_dropdown` draws a label above the field; "Raridade" gives it
    // the same label-above-field appearance as the "Box Style" dropdown.
    s.rarity_preview = kRarityFilterLabels[s.rarity_flt];
    widget_t rar_data;
    if (begin_dropdown("Raridade", s.rarity_preview, kRarityFilterN, rar_data))
    {
        for (int i = 0; i < kRarityFilterN; ++i)
        {
            if (selectable(kRarityFilterLabels[i], s.rarity_flt == i).value_changed)
            {
                s.rarity_flt = i;
                // Bust the filter cache so the list refreshes immediately.
                s.cache_rarity = -1;
            }
        }
        end_dropdown();
    }

    // ---- Skin list ---------------------------------------------------
    refresh_filter(s);

    const uint32_t active_id = active_id_for_cat(s.cat_tab);
    const float list_h = ImGui::GetContentRegionAvail().y - SCALE(2.f);
    const float row_height = ImGui::GetTextLineHeightWithSpacing() + SCALE(2.f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,
        draw->get_clr(clr->widgets.sub_stroke));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
        draw->get_clr(clr->widgets.stroke));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,
        draw->get_clr(clr->accent));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,     SCALE(4.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, SCALE(2.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,       ImVec2(0.f, SCALE(1.f)));

    ImGui::BeginChild("##sc_list",
        ImVec2(ImGui::GetContentRegionAvail().x, list_h),
        false, ImGuiWindowFlags_NoMove);

    ImDrawList* ldl = ImGui::GetWindowDrawList();

    if (s.filtered.empty()) {
        ImFont* fnt = font->get(onest_medium_data, 12);
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const float  rw = ImGui::GetContentRegionAvail().x;
        draw->text_clipped(ldl, fnt, cp, ImVec2(cp.x + rw, cp.y + SCALE(16.f)),
            draw->get_clr(clr->widgets.text_inactive),
            "Nenhuma skin encontrada.", nullptr, nullptr, ImVec2(0.f, 0.5f));
    } else {
        ImFont* fnt = font->get(onest_medium_data, 13);
        ImGuiListClipper clip;
        clip.Begin((int)s.filtered.size(), row_height);

        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const auto* e    = s.filtered[(size_t)i];
                const bool isAct = (e->id == active_id);
                const ImU32 rc   = rarity_color(e->rarity);

                const float item_w = ImGui::GetContentRegionAvail().x;
                const ImVec2 rmin  = ImGui::GetCursorScreenPos();
                const ImVec2 rmax  = ImVec2(rmin.x + item_w, rmin.y + row_height);

                // Hit area
                char hit_id[24]; std::snprintf(hit_id, sizeof(hit_id), "##row_%d", i);
                ImGui::InvisibleButton(hit_id, ImVec2(item_w, row_height));
                const bool hov     = ImGui::IsItemHovered();
                const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

                // Row background
                if (isAct) {
                    ldl->AddRectFilled(rmin, rmax,
                        draw->get_clr(clr->accent, 0.15f), SCALE(3.f));
                } else if (hov) {
                    ldl->AddRectFilled(rmin, rmax,
                        draw->get_clr(clr->widgets.background, 0.70f), SCALE(3.f));
                }

                // Left accent bar for active skin
                if (isAct) {
                    ldl->AddRectFilled(
                        ImVec2(rmin.x,           rmin.y + SCALE(4.f)),
                        ImVec2(rmin.x + SCALE(3.f), rmax.y - SCALE(4.f)),
                        draw->get_clr(clr->accent));
                }

                // Rarity dot
                const float dot_x = rmin.x + SCALE(12.f);
                const float dot_y = (rmin.y + rmax.y) * 0.5f;
                ldl->AddCircleFilled(ImVec2(dot_x, dot_y), SCALE(3.5f), rc, 10);

                // Skin name + ID
                char label[320];
                std::snprintf(label, sizeof(label), "  %s   #%u",
                    e->description ? e->description : "?", e->id);

                const float text_x = rmin.x + SCALE(22.f);
                draw->text_clipped(ldl, fnt,
                    ImVec2(text_x, rmin.y),
                    ImVec2(rmax.x - SCALE(4.f), rmax.y),
                    isAct ? draw->get_clr(clr->widgets.text)
                          : (hov ? draw->get_clr(clr->widgets.text)
                                 : draw->get_clr(clr->widgets.text_inactive)),
                    label, nullptr, nullptr, ImVec2(0.f, 0.5f));

                // 1-click apply
                if (clicked) {
                    if (s.cat_tab >= 0 && s.cat_tab < kCatDefCount) {
                        const SkinChanger::Category cat = kCatDefs[s.cat_tab].cat;
                        if (isAct)
                            SkinChanger::ClearWildcard(cat);    // toggle off
                        else
                            SkinChanger::SetWildcard(cat, e->id);
                    }
                }
            }
        }
        clip.End();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace detail

// -------------------------------------------------------------------------
// Section entry point.
// -------------------------------------------------------------------------
inline void render_panels()
{
    using namespace detail;
    SkinChanger::LoadDatabase();
    ui_state& s = g_ui();

    const float total_w = ImGui::GetContentRegionAvail().x;
    const float total_h = ImGui::GetContentRegionAvail().y;
    const float gap     = SCALE(8.f);
    const float tab_w   = SCALE(148.f);
    const float pick_w  = total_w - tab_w - gap;

    draw_left_child (s.cat_tab, tab_w,  total_h);
    ImGui::SameLine (0.f, gap);
    draw_right_child(s, pick_w, total_h);
}

}} // namespace sections::skinchanger
