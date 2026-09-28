#pragma once
#include <globals.hh>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <includes/imgui/imgui.h>
#include <includes/imgui/imgui_internal.h>
#include <includes/awesome/font_awesome.h>

// IMGUI_DEFINE_MATH_OPERATORS above already provided operator+/-/* etc for
// ImVec2 via imgui_internal.h. Skip the local set to avoid MSVC LNK2005 —
// clang-cl merges the duplicates silently, cl.exe rejects them.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) {
    return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

inline ImVec2 operator+(const ImVec2& lhs, float value) {
    return ImVec2(lhs.x + value, lhs.y + value);
}

inline ImVec2 operator+(float value, const ImVec2& rhs) {
    return ImVec2(value + rhs.x, value + rhs.y);
}

inline ImVec2 operator-(const ImVec2& a, const ImVec2& b) {
    return ImVec2(a.x - b.x, a.y - b.y);
}

inline ImVec2 operator*(const ImVec2& a, float b) {
    return ImVec2(a.x * b, a.y * b);
}

inline ImVec2 operator/(const ImVec2& a, float b) {
    return ImVec2(a.x / b, a.y / b);
}

inline ImVec2 operator/(const ImVec2& a, const ImVec2& b) {
    return ImVec2(a.x / b.x, a.y / b.y);
}
#endif // !IMGUI_DEFINE_MATH_OPERATORS

inline ImVec2 CalcTextSize(ImFont* font, int size, const char* label) {
    return font->CalcTextSizeA(size, FLT_MAX, 0, label);
}

#pragma warning( disable : 4996 )
