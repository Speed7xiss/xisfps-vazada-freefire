#pragma once
#include "../../../FrameWork/Dependencies/ImGui/imgui.h"

namespace EspFont {
    extern ImFont* Verdana;
    // Called once during ESP overlay ImGui setup, right before the font atlas
    // gets built. Safe to call multiple times — first call wins.
    void Initialize();
    // Clears cached font pointers so the next Initialize() rebuilds against
    // the current ImGui context. MUST be called when the ImGui context or
    // its font atlas is being torn down (e.g. emulator close → new cycle).
    void Reset();
}
