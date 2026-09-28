#include "CheatManager.hpp"

#include "Cheat.hpp"
#include "CheatStatus.hpp"
#include "Options.hpp"

#include <windows.h>
#include <TlHelp32.h>
#include <thread>
#include <atomic>

// ─── Wrapper SEH para Cheat::Initialize ──────────────────────────────────────
// Cheat::Initialize() pode lançar SEH (AV, stack overflow, etc.) durante a
// leitura de estruturas do emulador. Sem captura, a exceção propaga para
// std::terminate() → abort() → mata o processo host injetado.
//
// A função wrapper é SEPARADA do lambda para garantir que não há objetos C++
// com destrutor no mesmo frame que o __try (comportamento definido com /EHsc).
// ─────────────────────────────────────────────────────────────────────────────
static void SafeCheatInitialize()
{
    __try
    {
        Cheat::Initialize();
    }
    __except( EXCEPTION_EXECUTE_HANDLER )
    {
        // Exceção SEH capturada — impede std::terminate().
        // Reseta para WaitingEmulator para que o monitor retente na
        // próxima iteração sem matar o processo host injetado.
        CheatStatus::SetPhase( CheatStatus::WaitingEmulator );
    }
}

HMODULE g_hModule = nullptr;

namespace CheatManager {

    static std::atomic<bool> s_monitorRunning{ false };
    static std::atomic<bool> s_cheatActive   { false };

    static constexpr const wchar_t* kTargets[] = {
        L"HD-Player.exe",
        L"LdVBoxHeadless.exe",
        L"dnplayer.exe",
        L"MuMuVMMHeadless.exe",
        L"Oxy.exe",
        L"FFNA-Server.exe",
        nullptr
    };

    static DWORD FindProcByName(const wchar_t* name)
    {
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W pe{ sizeof(pe) };
        DWORD pid = 0;
        if (::Process32FirstW(snap, &pe))
            do {
                if (!_wcsicmp(pe.szExeFile, name)) { pid = pe.th32ProcessID; break; }
            } while (::Process32NextW(snap, &pe));
        ::CloseHandle(snap);
        return pid;
    }

    static void MonitorLoop()
    {
        if (!CheatStatus::IsError(CheatStatus::GetPhase()))
            CheatStatus::SetPhase(CheatStatus::WaitingEmulator);

        while (s_monitorRunning.load(std::memory_order_relaxed))
        {
            if (s_cheatActive.load())
            {
                ::Sleep(2000);
                continue;
            }

            bool found = false;
            for (int i = 0; kTargets[i] != nullptr; ++i)
            {
                if (FindProcByName(kTargets[i]) != 0)
                {
                    found = true;
                    if (!CheatStatus::IsError(CheatStatus::GetPhase()))
                        CheatStatus::SetPhase(CheatStatus::WaitingGame);
                    break;
                }
            }

            if (!found)
            {
                if (!CheatStatus::IsError(CheatStatus::GetPhase()))
                    CheatStatus::SetPhase(CheatStatus::WaitingEmulator);
                ::Sleep(2000);
                continue;
            }

            ::Sleep(3000);
            if (!s_monitorRunning.load()) break;

            s_cheatActive.store(true);
            CheatStatus::SetPhase(CheatStatus::Starting);

            std::thread([]()
            {
                SafeCheatInitialize();

                s_cheatActive.store(false);
                if (!CheatStatus::IsError(CheatStatus::GetPhase()))
                    CheatStatus::SetPhase(CheatStatus::WaitingEmulator);

                g_Options.General.ShutDown = false;
                g_Options.General.Restart  = false;

            }).detach();

            ::Sleep(2000);
        }
    }

    void Start()
    {
        if (s_monitorRunning.exchange(true, std::memory_order_acq_rel)) return;
        std::thread(MonitorLoop).detach();
    }

    void Stop()
    {
        s_monitorRunning.store(false, std::memory_order_release);
        g_Options.General.ShutDown = true;
        for (int i = 0; i < 10 && s_cheatActive.load(); ++i)
            ::Sleep(300);
    }

} // namespace CheatManager
