#pragma once
#include <cstdint>

namespace Cheat {
    namespace BugarPixel {
        // GameVarDef chain (FreeFire OB v7a 69):
        //   il2cpp + GameVarDef_TypeInfo  -> typeInfo (Il2CppClass*)
        //   typeInfo + StaticFields       -> gameVarDef (static_fields)
        //   gameVarDef + EnableShootTraceAdjustment = bool (force false)
        // Atualizado pra versao atual (delta confirmado script.json: GameFacade -> GameVarDef = +0x54)
        // EnableShootTraceAdjustment: dump.cs linha 292596 confirma 0x670
        constexpr uint32_t GameVarDef_TypeInfo          = 0xA5BC398; // atualizado 2026-09-16 (era 0xA342F50) - script.json Address 173786008 COW.GameVarDef_TypeInfo
        constexpr uint32_t GameVarDef_StaticFields      = 0x5C;
        constexpr uint32_t EnableShootTraceAdjustment   = 0x728;     // atualizado 2026-09-16 do dump v7a 1.132.1 (era 0x670)

        // Thread loop period (ms).
        constexpr int LoopIntervalMs = 50;

        void Start(uint32_t il2cppBase);
        void Stop(); // Added Stop for cleanup
    }
}
