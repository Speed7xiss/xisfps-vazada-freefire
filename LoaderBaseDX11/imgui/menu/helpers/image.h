#pragma once
#include "imgui.h"
#include <memory>

class c_image
{
public:
    ImTextureID load(const unsigned char* bytes, int len);
};

inline std::unique_ptr<c_image> image = std::make_unique<c_image>();
