#include "AntiAttach.hpp"
#include "AntiDbg.hpp"
#include "../XorStr/XorStr.hpp"

#include <Windows.h>
#include <atomic>
#include <thread>
#include <chrono>

// Intervalo do WatchLoop em ms
static constexpr DWORD ATTACH_WATCH_INTERVAL_MS = 5000;

// ─── NT type ──────────────────────────────────────────────────────────────────
namespace {
    typedef LONG ( NTAPI* fnNtSetInformationThread )( HANDLE, ULONG, PVOID, ULONG );
    static constexpr ULONG ThreadHideFromDebugger = 0x11;

    // Cached — ntdll nunca descarrega.
    static fnNtSetInformationThread s_NtSIT = nullptr;

    static fnNtSetInformationThread GetNtSIT() {
        if ( !s_NtSIT ) {
            HMODULE ntdll = GetModuleHandleA( xorstr_("ntdll.dll").crypt_get() );
            if ( ntdll )
                s_NtSIT = reinterpret_cast<fnNtSetInformationThread>(
                    GetProcAddress( ntdll, xorstr_("NtSetInformationThread").crypt_get() ) );
        }
        return s_NtSIT;
    }

    // ── Patch helper ──────────────────────────────────────────────────────────
    // Substitui os primeiros bytes de 'fn' por um stub RET (C3).
    // Antes: salva proteção, escreve, restaura.
    static void PatchWithRet( const char* funcName ) {
        HMODULE ntdll = GetModuleHandleA( xorstr_("ntdll.dll").crypt_get() );
        if ( !ntdll ) return;

        void* fn = GetProcAddress( ntdll, funcName );
        if ( !fn ) return;

        // x64 RET stub: [C3] — 1 byte é suficiente para abortar a função
        // imediatamente sem crash (stack está limpa no prologue-before-push).
        static constexpr BYTE RET_STUB[] = { 0xC3 };

        DWORD old = 0;
        if ( VirtualProtect( fn, sizeof( RET_STUB ), PAGE_EXECUTE_READWRITE, &old ) ) {
            memcpy( fn, RET_STUB, sizeof( RET_STUB ) );
            VirtualProtect( fn, sizeof( RET_STUB ), old, &old );
            FlushInstructionCache( GetCurrentProcess(), fn, sizeof( RET_STUB ) );
        }
    }
} // anonymous namespace

// ─── Public API ───────────────────────────────────────────────────────────────

void AntiDbg::Attach::HideThread( HANDLE hThread ) {
    auto NtSIT = GetNtSIT();
    if ( !NtSIT ) return;
    // ThreadHideFromDebugger remove a thread da lista de threads visíveis ao
    // debugger — ela não recebe eventos de debug (breakpoints, single-step, etc.)
    NtSIT( hThread, ThreadHideFromDebugger, nullptr, 0 );
}

void AntiDbg::Attach::PatchBreakpointFunctions() {
    // ntdll!DbgBreakPoint — chamado quando o debugger já está anexado e quer
    // emitir INT3 no contexto do processo (ex.: Ctrl+Break no WinDbg).
    PatchWithRet( xorstr_("DbgBreakPoint").crypt_get() );

    // ntdll!DbgUiRemoteBreakin — thread que o debugger injeta via
    // CreateRemoteThread ao fazer attach (SetWindowsHookEx path).
    // Com isso patchado, o attach simplesmente retorna sem efeito.
    PatchWithRet( xorstr_("DbgUiRemoteBreakin").crypt_get() );

    // NOTA: NtContinue NÃO é patchado aqui.
    // NtContinue é chamado internamente pelo kernel para retomar execução
    // após __try/__except handlers — o cheat usa isso extensivamente.
    // Patchar NtContinue quebraria todas as cláusulas __except do cheat.
}

void AntiDbg::Attach::WatchLoop() {
    // Roda em thread própria (ver AntiDbg.cpp).
    // Re-patcha periodicamente caso alguém restaure as funções.
    while ( true ) {
        PatchBreakpointFunctions();
        std::this_thread::sleep_for( std::chrono::milliseconds( ATTACH_WATCH_INTERVAL_MS ) );
    }
}
