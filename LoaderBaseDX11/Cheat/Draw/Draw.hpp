#pragma once
#include <Windows.h>
#include "../Memory/Memory.hpp"
#include "../Math/Vectors/Vector3.hpp"

class Data {
public:
    static void Draw(HWND hWindow, uint32_t il2cpp, int width, int height);
    // Clears the cached match/entity pointer chain and per-entity caches so
    // stale pointers from the previous emulator instance don't leak into the
    // next cycle. Call on emulator shutdown.
    static void Reset();
};
