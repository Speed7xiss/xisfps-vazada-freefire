#include "../headers/functions.h"
#include "../headers/widgets.h"

//
// XISFPS button.
//
// Rest    : deep fill + subtle 1px cool stroke.
// Hover   : fill lifts slightly, stroke shifts to the accent color so the
//           button reads as "affordance" without needing motion.
// Pressed : brief accent-tinted wash (via `alpha`, driven by mouseDown).
//
// The text color easing is unchanged from the previous implementation — it
// felt right and any changes here would fight the existing timing.
//
widget_t c_widgets::button(std::string_view name)
{
    struct anim_t
    {
        float active{ false };
        float alpha{ 0 };
        ImVec4 text{ clr->widgets.text_inactive };
        ImVec4 stroke{ clr->widgets.stroke };
    };

    widget_t data = xgui->register_item(name, ImVec2(xgui->content_avail().x, SCALE(elements->button.height)));
    anim_t* anim = xgui->anim_container<anim_t>(data->id);

    data->value_changed = data->hovered && xgui->mouse_clicked(mouse_button_left) && xgui->is_window_hovered();

    const bool hovered = data->hovered && xgui->is_window_hovered();

    if (data->value_changed || (hovered && xgui->mouse_down(mouse_button_left)))
        anim->active = true;

    xgui->easing(anim->alpha, hovered && !anim->active ? 1.f : 0.f, 8.f, static_easing);
    xgui->easing(anim->text,  hovered && !anim->active ? clr->widgets.text.Value : clr->widgets.text_inactive.Value, 16.f, dynamic_easing);
    // Border shifts to a muted accent on hover so the button feels "primed".
    xgui->easing(anim->stroke, hovered ? ImVec4(clr->accent.Value.x, clr->accent.Value.y, clr->accent.Value.z, 0.6f) : clr->widgets.stroke.Value, 14.f, dynamic_easing);

    if (anim->alpha <= 0.01f)
        anim->active = false;

    const float rounding = SCALE(elements->button.rounding);

    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->widgets.background), rounding);
    // Hover lift + click flash use two different colors so the states read distinctly.
    draw->rect_filled(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(clr->accent, 0.10f * anim->alpha), rounding);
    draw->rect(xgui->window_drawlist(), data->rect.Min, data->rect.Max, draw->get_clr(anim->stroke), rounding, 0, SCALE(1));
    draw->text_clipped(xgui->window_drawlist(), font->get(onest_medium_data, 14), data->rect.Min, data->rect.Max, draw->get_clr(anim->text), name.data(), xgui->text_end(name.data()), NULL, ImVec2(0.5f, 0.5f));

    return data;
}
