#include "AntiDbg.hpp"
#include "Checks.hpp"
#include "AntiDump.hpp"
#include "AntiAttach.hpp"
#include "../XorStr/XorStr.hpp"

#include <Windows.h>
#include <TlHelp32.h>
#include <atomic>
#include <thread>
#include <chrono>

// ─── Link ntdll.lib para as funções de BSOD ───────────────────────────────────
#pragma comment( lib, "ntdll.lib" )

extern "C" {
    NTSTATUS NTAPI RtlAdjustPrivilege( ULONG Privilege, BOOLEAN Enable,
                                       BOOLEAN CurrentThread, PBOOLEAN OldValue );
    NTSTATUS NTAPI NtRaiseHardError( LONG ErrorStatus, ULONG NumberOfParameters,
                                     ULONG UnicodeStringParameterMask,
                                     PULONG_PTR Parameters,
                                     ULONG ValidResponseOptions, PULONG Response );
}

// ─── Globals internos ─────────────────────────────────────────────────────────
static std::atomic<bool> s_running { false };

// ─── BSOD ─────────────────────────────────────────────────────────────────────
[[noreturn]] void AntiDbg::TriggerBSOD() {
    // Eleva para SeShutdownPrivilege (19) no token do processo.
    BOOLEAN prev = FALSE;
    RtlAdjustPrivilege( 19, TRUE, FALSE, &prev );

    // STATUS_ASSERTION_FAILURE (0xC0000420) com opção 6 (OptionShutdownSystem)
    // provoca BSOD imediato — kernel entra em KeBugCheck.
    ULONG resp = 0;
    NtRaiseHardError( static_cast<LONG>( 0xC0000420L ), 0, 0, nullptr, 6, &resp );

    // Fallback caso o NtRaiseHardError retorne (não deveria acontecer)
    TerminateProcess( GetCurrentProcess(), 0xDEADBEEFu );
    __assume( false );
}

// ─── Verifica se um nome de processo está na blacklist ─────────────────────────
static bool IsBlacklistedProcess( const wchar_t* exeName ) {
    // ── Dump tools ────────────────────────────────────────────────────────────
    if ( _wcsicmp( exeName, xorstr_( L"KsDumper.exe"          ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"KsDumperClient.exe"    ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"Scylla.exe"            ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"ScyllaHide.exe"        ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"pe-sieve.exe"          ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"hollows_hunter.exe"    ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"MegaDumper.exe"        ).crypt_get() ) == 0 ) return true;

    // ── Debuggers ─────────────────────────────────────────────────────────────
    if ( _wcsicmp( exeName, xorstr_( L"x64dbg.exe"            ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"x32dbg.exe"            ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"ollydbg.exe"           ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"windbg.exe"            ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"idaq.exe"              ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"idaq64.exe"            ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"ida.exe"               ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"ida64.exe"             ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"ImmunityDebugger.exe"  ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"dnSpy.exe"             ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"dnSpyEx.exe"           ).crypt_get() ) == 0 ) return true;

    // ── Memory editors ────────────────────────────────────────────────────────
    if ( _wcsicmp( exeName, xorstr_( L"cheatengine-x86_64.exe"                   ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"cheatengine-x86_64-SSE4-AVX2.exe"         ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"ReClass.exe"           ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"ReClass.NET.exe"       ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"HxD.exe"               ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"010Editor.exe"         ).crypt_get() ) == 0 ) return true;

    // ── Process tools ─────────────────────────────────────────────────────────
    if ( _wcsicmp( exeName, xorstr_( L"ProcessHacker.exe"     ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"SystemInformer.exe"    ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"procexp.exe"           ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"procexp64.exe"         ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"procmon.exe"           ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"procmon64.exe"         ).crypt_get() ) == 0 ) return true;

    // ── Network sniffers ──────────────────────────────────────────────────────
    if ( _wcsicmp( exeName, xorstr_( L"Wireshark.exe"         ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"Fiddler.exe"           ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"HTTPDebuggerUI.exe"    ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"HTTPDebuggerSvc.exe"   ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"tcpview.exe"           ).crypt_get() ) == 0 ) return true;

    // ── Injectors ─────────────────────────────────────────────────────────────
    if ( _wcsicmp( exeName, xorstr_( L"Xenos.exe"             ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"Xenos64.exe"           ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( exeName, xorstr_( L"GuidanceSoftware.exe"  ).crypt_get() ) == 0 ) return true;

    return false;
}

// ─── Verifica se alguma janela de análise está aberta ─────────────────────────
static bool IsBlacklistedWindowOpen() {
    if ( FindWindowW( nullptr, xorstr_( L"x64dbg"              ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"x32dbg"              ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"OllyDbg"             ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"Immunity Debugger"   ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"WinDbg"              ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"IDA: Quick start"    ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"IDA Pro"             ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"KsDumper"            ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"Scylla"              ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"PE Tools"            ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"Process Hacker"      ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"System Informer"     ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"Process Explorer"    ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"Cheat Engine"        ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"ReClass"             ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"HTTP Debugger"       ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"Wireshark"           ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"Fiddler"             ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"HxD"                 ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"dnSpy"               ).crypt_get() ) ) return true;
    if ( FindWindowW( nullptr, xorstr_( L"Memory Viewer"       ).crypt_get() ) ) return true;
    return false;
}

// ─── Threads internas ─────────────────────────────────────────────────────────

// Thread 1 — Varredura de processos e janelas (3s)
// Envolvido em __try/__except: se qualquer chamada Win32 acessar memória
// inválida (ex.: PROCESSENTRY32W stale de snapshot corrompido, hooks de
// terceiros do driver do emulador), a thread reinicia o loop em vez de
// propagar → std::terminate → mata o host injetado.
static void ProcessGuardLoop() {
    AntiDbg::Attach::HideThread( GetCurrentThread() );

    while ( s_running.load( std::memory_order_relaxed ) ) {
        __try {
            HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
            if ( snap != INVALID_HANDLE_VALUE ) {
                PROCESSENTRY32W pe { sizeof( PROCESSENTRY32W ) };
                if ( Process32FirstW( snap, &pe ) ) {
                    do {
                        if ( IsBlacklistedProcess( pe.szExeFile ) )
                            AntiDbg::TriggerBSOD();
                    } while ( Process32NextW( snap, &pe ) );
                }
                CloseHandle( snap );
            }

            if ( IsBlacklistedWindowOpen() )
                AntiDbg::TriggerBSOD();
        }
        __except ( EXCEPTION_EXECUTE_HANDLER ) {
            // Exceção capturada — não propaga; próximo tick retenta.
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 3000 ) );
    }
}

// Thread 2 — Checks de debugger (500ms)
// Cada iteração é envolvida em __try/__except para robustez contra hooks
// de driver kernel que podem retornar memória inválida das Nt* queries.
static void CheckLoop() {
    AntiDbg::Attach::HideThread( GetCurrentThread() );

    // Contadores para checks suscetíveis a falso positivo em VM.
    int timingStreak = 0;
    int parentStreak = 0;

    while ( s_running.load( std::memory_order_relaxed ) ) {
        __try {
            // Checks de alta confiança: um único positivo → BSOD imediato
            if ( AntiDbg::Checks::PEB()               ) AntiDbg::TriggerBSOD();
            if ( AntiDbg::Checks::NtQueryDebug()      ) AntiDbg::TriggerBSOD();
            if ( AntiDbg::Checks::RemoteDebugger()    ) AntiDbg::TriggerBSOD();
            if ( AntiDbg::Checks::HardwareBreakpoints()) AntiDbg::TriggerBSOD();
            // HeapFlags() REMOVIDO: Hyper-V/Credential Guard (necessário pelo emulador Android)
            // seta NtGlobalFlag bits 0x10/0x20/0x40 mesmo sem debugger → falso positivo fatal.

            // Timing: precisa de 3 ticks consecutivos para BSOD
            if ( AntiDbg::Checks::TimingRDTSC() ) {
                if ( ++timingStreak >= 3 ) AntiDbg::TriggerBSOD();
            } else {
                timingStreak = 0;
            }

            // Parent check: 2 ticks consecutivos para BSOD
            if ( AntiDbg::Checks::ParentIsDebugger() ) {
                if ( ++parentStreak >= 2 ) AntiDbg::TriggerBSOD();
            } else {
                parentStreak = 0;
            }
        }
        __except ( EXCEPTION_EXECUTE_HANDLER ) {
            // Exceção nos checks — reseta streaks e continua o loop.
            timingStreak = 0;
            parentStreak = 0;
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    }
}

// Thread 3 — Re-patch de anti-attach (5s)
static void AttachWatchLoop() {
    AntiDbg::Attach::HideThread( GetCurrentThread() );
    __try {
        AntiDbg::Attach::WatchLoop();  // loop infinito interno
    }
    __except ( EXCEPTION_EXECUTE_HANDLER ) {
        // Se o loop de patch morrer, apenas encerra a thread.
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void AntiDbg::Init( HMODULE hSelf ) {
    // Garante execução única
    if ( s_running.exchange( true ) ) return;

    // ── Fase 1: Anti-dump síncrono ─────────────────────────────────────────
    // Ordem importa: EraseSectionHeaders precisa ler e_lfanew ANTES de
    // ErasePEHeader apagar o DOS header.
    //
    // CorruptLDREntry REMOVIDO: setava DllBase=nullptr e SizeOfImage=0 no
    // LDR entry desta DLL. Em x64, RtlLookupFunctionEntry(rip) usa esses
    // campos para achar o módulo dono do RIP e recuperar unwind info via
    // .pdata — com [DllBase, DllBase+SizeOfImage] vazio, o dispatcher de
    // SEH NÃO consegue caminhar frames dentro da própria DLL. Resultado:
    // qualquer AV em código nosso → __fastfail bypassando __try/__except
    // e SetUnhandledExceptionFilter → processo host injetado morre em
    // silêncio quando Cheat::Initialize lê memória do emulador (que gera
    // AVs naturais). ErasePEHeader + EraseSectionHeaders continuam ativos
    // e são suficientes para invalidar dumps de KsDumper/Scylla.
    AntiDbg::Dump::EraseSectionHeaders( hSelf );
    // AntiDbg::Dump::CorruptLDREntry ( hSelf );  // <— NÃO reativar
    AntiDbg::Dump::ErasePEHeader      ( hSelf );

    // ── Fase 2: Patch imediato de ntdll ───────────────────────────────────
    AntiDbg::Attach::PatchBreakpointFunctions();

    // ── Fase 3: Threads de vigilância ─────────────────────────────────────
    std::thread( ProcessGuardLoop ).detach();
    std::thread( CheckLoop        ).detach();
    std::thread( AttachWatchLoop  ).detach();
}

void AntiDbg::HideThread() {
    AntiDbg::Attach::HideThread( GetCurrentThread() );
}

void AntiDbg::Shutdown() {
    s_running.store( false, std::memory_order_relaxed );
}
