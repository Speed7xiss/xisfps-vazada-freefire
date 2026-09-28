#include "../headers/functions.h"
#include "../headers/widgets.h"

//
// XISFPS child card — MultiLoader-styled panels.
//
// KEY BEHAVIORS
//   1. Fixed height — every card takes the full available content-pane height.
//   2. Internal scroll — when the card's contents overflow, ImGui's default
//      scrollbar appears and mouse wheel scrolls the card in place, matching
//      the MultiLoader's CustomChild pattern.
//   3. Title OUTSIDE the card — the titled overload draws its heading in the
//      space ABOVE the card (like "SELECT YOUR LOADER" / "INFO" in the
//      MultiLoader, which sit above their respective panels, not inside).
//
// Two flavors:
//   begin_child(name)            — untitled panel
//   begin_child(name, title)     — panel with a small dim-grey heading placed
//                                  above the card, and the card starting
//                                  below it (same total footprint as before).
//

static const float TITLE_STRIP    = 22.f;  // space reserved ABOVE the card for the heading
static const float TITLE_GAP      = 6.f;   // vertical gap between heading and card top

static void draw_card_frame(const ImVec2& min, const ImVec2& max)
{
    const float r = SCALE(elements->child.rounding);
    draw->rect_filled(xgui->window_drawlist(), min, max, draw->get_clr(clr->window.sub_background), r);
    draw->rect(xgui->window_drawlist(), min, max, draw->get_clr(clr->widgets.stroke), r, 0, SCALE(1));
}

// Compute the fixed card height available in the current content pane.
//   The card should stretch from its Y origin down to the bottom of the
//   content window so cards always share the same visual footprint.
static float compute_card_height(const ImVec2& pos)
{
    ImGuiWindow* win = xgui->get_window();
    const float pane_bottom = win->WorkRect.Max.y;
    float h = pane_bottom - pos.y;
    if (h < SCALE(80.f)) h = SCALE(80.f);
    return h;
}

void c_widgets::begin_child(std::string_view name)
{
    struct child_state
    {
        float width{ 0 };
    };

    ImGuiWindow* window = xgui->get_window();
    const ImVec2 pos = window->DC.CursorPos;

    child_state* anim = xgui->anim_container<child_state>(window->GetID(name.data()));
    anim->width = (xgui->content_max().x - var->style.item_spacing.x - var->style.window_padding.x) / 2.f;

    const float card_h = compute_card_height(pos);

    draw_card_frame(pos, pos + ImVec2(anim->width, card_h));

    xgui->push_var(style_var_window_padding, SCALE(elements->child.padding));
    xgui->begin_def_child(name.data() + std::string("content_child"),
        ImVec2(anim->width, card_h), 0,
        window_flags_always_use_window_padding | window_flags_no_move |
        window_flags_nav_flattened | window_flags_no_saved_settings);
    xgui->push_var(style_var_item_spacing, SCALE(elements->child.spacing));
}

void c_widgets::begin_child(std::string_view name, std::string_view title)
{
    struct child_state
    {
        float width{ 0 };
    };

    ImGuiWindow* window = xgui->get_window();
    const ImVec2 pos_top = window->DC.CursorPos;

    child_state* anim = xgui->anim_container<child_state>(window->GetID(name.data()));
    anim->width = (xgui->content_max().x - var->style.item_spacing.x - var->style.window_padding.x) / 2.f;

    // --- Heading OUTSIDE the card (MultiLoader style) ---
    // Small dim-grey caption ("Aim Assistance", "Aim Settings") lives in the
    // strip above the panel, aligned to the panel's left edge. The card
    // starts TITLE_STRIP + TITLE_GAP px below the current cursor.
    ImFont* title_fnt = font->get(onest_medium_data, SCALE(13));
    const ImVec2 title_min = pos_top;
    const ImVec2 title_max{ pos_top.x + anim->width, pos_top.y + SCALE(TITLE_STRIP) };
    draw->text_clipped(xgui->window_drawlist(), title_fnt, title_min, title_max,
        draw->get_clr(ImVec4(0.55f, 0.55f, 0.55f, 1.f)),
        title.data(), xgui->text_end(title.data()), NULL, ImVec2(0.f, 0.5f));

    // --- Card sits below the heading ---
    const ImVec2 card_pos{ pos_top.x, pos_top.y + SCALE(TITLE_STRIP + TITLE_GAP) };
    const float  card_h = compute_card_height(card_pos);

    draw_card_frame(card_pos, card_pos + ImVec2(anim->width, card_h));

    // Push the cursor to the card top-left so BeginChild opens at the right place.
    ImGui::SetCursorScreenPos(card_pos);

    xgui->push_var(style_var_window_padding, SCALE(elements->child.padding));
    xgui->begin_def_child(name.data() + std::string("content_child"),
        ImVec2(anim->width, card_h), 0,
        window_flags_always_use_window_padding | window_flags_no_move |
        window_flags_nav_flattened | window_flags_no_saved_settings);
    xgui->push_var(style_var_item_spacing, SCALE(elements->child.spacing));
}

void c_widgets::end_child()
{
    xgui->pop_var();
    xgui->end_def_child();
    xgui->pop_var();
}
