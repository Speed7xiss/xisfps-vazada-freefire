#include "BugarPixel.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include "Memory/Memory.hpp"
#include "Options.hpp"

namespace Cheat {
    namespace BugarPixel {

        static std::atomic<bool> g_Running{ false };

        static uint32_t ResolveGameVarDef(uint32_t il2cppBase) {
            if (!il2cppBase) return 0;

            uint32_t typeInfo = g_Memory->Read<uint32_t>(il2cppBase + GameVarDef_TypeInfo);
            if (!typeInfo) return 0;

            uint32_t gameVarDef = g_Memory->Read<uint32_t>(typeInfo + GameVarDef_StaticFields);
            return gameVarDef;
        }

        static void Loop(uint32_t il2cppBase) {
            uint32_t cachedGameVarDef = 0;

            while (g_Running.load() && !g_Options.General.ShutDown) {
                if (cachedGameVarDef == 0) {
                    cachedGameVarDef = ResolveGameVarDef(il2cppBase);
                }

                if (cachedGameVarDef != 0 && g_Options.AimDMA.BugarPixel) {
                    uint32_t flagAddr = cachedGameVarDef + EnableShootTraceAdjustment;
                    
                    uint8_t value = 0;
                    bool ok = g_Memory->Write<uint8_t>(flagAddr, value);
                    if (!ok) {
                        cachedGameVarDef = 0;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(LoopIntervalMs));
            }
            g_Running = false;
        }

        void Start(uint32_t il2cppBase) {
            if (g_Running.exchange(true)) return;
            std::thread(Loop, il2cppBase).detach();
        }

        void Stop() {
            g_Running = false;
        }
    }
}
