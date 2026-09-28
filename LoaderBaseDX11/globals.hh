#pragma once
#include <includes/imgui/imgui.h>
#include <d3d11.h>
#include <windows.h>

// Dimensões e HWND da janela do menu (usados pelo XISFPS widget stack).
struct c_globals
{
    HWND hwnd  { nullptr };
    int  width { 720 };
    int  height{ 460 };
};
inline c_globals globals;

// Structs de funções legadas — mantidas para compatibilidade com
// config.cpp (ref: functions.aim.enabled, functions.exploits.streamMode).
struct c_functions
{
    struct { bool enabled = false; } aim;
    struct { bool streamMode = true; } exploits;
};
inline c_functions functions;

// Contexto ImGui da GUI (compartilhado entre TUs do menu).
// O overlay ESP (Cheat::Initialize) usa um contexto SEPARADO (s_overlayCtx em Cheat.cpp).
inline ImGuiContext* g_guiImguiCtx{ nullptr };
