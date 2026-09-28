#include "Checks.hpp"
#include "../XorStr/XorStr.hpp"

#include <Windows.h>
#include <intrin.h>
#include <TlHelp32.h>

// ─── NT types (resolução dinâmica, sem link estático ao ntdll export) ─────────
namespace {
    typedef LONG ( NTAPI* fnNtQueryInformationProcess )(
        HANDLE, ULONG, PVOID, ULONG, PULONG );

    // Cached uma vez — ntdll não descarrega nunca.
    static fnNtQueryInformationProcess s_NtQIP = nullptr;

    static fnNtQueryInformationProcess GetNtQIP() {
        if ( !s_NtQIP ) {
            HMODULE ntdll = GetModuleHandleA( xorstr_("ntdll.dll").crypt_get() );
            if ( ntdll )
                s_NtQIP = reinterpret_cast<fnNtQueryInformationProcess>(
                    GetProcAddress( ntdll, xorstr_("NtQueryInformationProcess").crypt_get() ) );
        }
        return s_NtQIP;
    }
} // anonymous namespace

// ─── 1. PEB.BeingDebugged ─────────────────────────────────────────────────────
bool AntiDbg::Checks::PEB() {
    // PEB está em GS:[0x60] em x64.
    // BeingDebugged está no offset 0x02.
    const auto peb = reinterpret_cast<PBYTE>( __readgsqword( 0x60 ) );
    if ( !peb ) return false;
    return peb[ 0x02 ] != 0;
}

// ─── 2. NtQueryInformationProcess (3 classes) ─────────────────────────────────
bool AntiDbg::Checks::NtQueryDebug() {
    auto NtQIP = GetNtQIP();
    if ( !NtQIP ) return false;

    // ProcessDebugPort (0x07): != 0 quando há debugger.
    // Checamos o NTSTATUS: se a query falhar (hook de driver do emulador,
    // permissão negada, etc.), NÃO reportamos "debugger presente" — o valor
    // ficaria stale/zero e um retorno true aqui dispara BSOD/TerminateProcess.
    HANDLE debugPort = nullptr;
    LONG status = NtQIP( GetCurrentProcess(), 0x07, &debugPort, sizeof( debugPort ), nullptr );
    if ( status >= 0 && debugPort ) return true;

    // ProcessDebugObjectHandle (0x1E): handle válido quando debugger presente
    HANDLE debugObj = nullptr;
    status = NtQIP( GetCurrentProcess(), 0x1E, &debugObj, sizeof( debugObj ), nullptr );
    if ( status >= 0 && debugObj ) {
        CloseHandle( debugObj );
        return true;
    }

    // ProcessDebugFlags (0x1F): 0 = debugado, 1 = normal.
    // BUG CRÍTICO ANTES: se NtQIP falhasse (hook de driver do emulador
    // filtrando classes de query), debugFlags ficava no valor inicial 0
    // → return true → TriggerBSOD → TerminateProcess. Agora só reportamos
    // debug se o NtStatus foi sucesso E o valor lido é realmente 0.
    ULONG debugFlags = 0xFFFFFFFF; // sentinela para diferenciar "não retornado"
    status = NtQIP( GetCurrentProcess(), 0x1F, &debugFlags, sizeof( debugFlags ), nullptr );
    if ( status >= 0 && debugFlags == 0 ) return true;

    return false;
}

// ─── 3. CheckRemoteDebuggerPresent ────────────────────────────────────────────
bool AntiDbg::Checks::RemoteDebugger() {
    BOOL present = FALSE;
    CheckRemoteDebuggerPresent( GetCurrentProcess(), &present );
    return present != FALSE;
}

// ─── 4. Hardware breakpoints (debug registers) ────────────────────────────────
bool AntiDbg::Checks::HardwareBreakpoints() {
    CONTEXT ctx {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    if ( !GetThreadContext( GetCurrentThread(), &ctx ) )
        return false;

    // Dr0–Dr3: endereços de breakpoint. Dr7: control register (habilita/desabilita).
    // Qualquer Dr0–Dr3 não-nulo ou Dr7 com bits locais ativos indica HW bp ativo.
    return ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3 || ( ctx.Dr7 & 0xFF );
}

// ─── 5. PEB.NtGlobalFlag ──────────────────────────────────────────────────────
bool AntiDbg::Checks::HeapFlags() {
    // NtGlobalFlag está em PEB+0xBC (x64).
    // Debugger seta: FLG_HEAP_ENABLE_TAIL_CHECK (0x10)
    //                FLG_HEAP_ENABLE_FREE_CHECK  (0x20)
    //                FLG_HEAP_VALIDATE_PARAMS     (0x40)
    // Máscara 0x70 cobre os três.
    const auto peb    = reinterpret_cast<PBYTE>( __readgsqword( 0x60 ) );
    if ( !peb ) return false;
    const ULONG ntGlobalFlag = *reinterpret_cast<const ULONG*>( peb + 0xBC );
    return ( ntGlobalFlag & 0x70 ) != 0;
}

// ─── 6. RDTSC timing ──────────────────────────────────────────────────────────
bool AntiDbg::Checks::TimingRDTSC() {
    // Dois RDTSC consecutivos: normalmente <200 ciclos em hardware bare-metal.
    // Com single-step / trap flag: ~100k+ ciclos.
    // Threshold elevado para 500.000: emuladores Android (BlueStacks/LDPlayer)
    // usam Hyper-V ou HAXM que virtualiza RDTSC, causando deltas de 50k-200k
    // ciclos em condições normais — um threshold baixo gera falso positivo fatal.
    const UINT64 t1 = __rdtsc();
    const UINT64 t2 = __rdtsc();
    return ( t2 - t1 ) > 500000ULL;
}

// ─── 7. Parent process check ──────────────────────────────────────────────────
bool AntiDbg::Checks::ParentIsDebugger() {
    auto NtQIP = GetNtQIP();
    if ( !NtQIP ) return false;

    // ProcessBasicInformation (0x00) contém InheritedFromUniqueProcessId
    struct PROCESS_BASIC_INFORMATION {
        PVOID     Reserved1;
        PVOID     PebBaseAddress;
        PVOID     Reserved2[ 2 ];
        ULONG_PTR UniqueProcessId;
        ULONG_PTR InheritedFromUniqueProcessId;
    } pbi {};

    if ( NtQIP( GetCurrentProcess(), 0x00, &pbi, sizeof( pbi ), nullptr ) != 0 )
        return false;

    const DWORD ppid = static_cast<DWORD>( pbi.InheritedFromUniqueProcessId );
    if ( !ppid ) return false;

    // Abre o pai com permissão mínima
    HANDLE hParent = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ppid );
    if ( !hParent ) return false;  // processo sistema — não é debugger

    wchar_t fullPath[ MAX_PATH ] {};
    DWORD   len = MAX_PATH;
    const BOOL ok = QueryFullProcessImageNameW( hParent, 0, fullPath, &len );
    CloseHandle( hParent );

    if ( !ok || len == 0 ) return false;

    // Extrai só o nome do arquivo
    const wchar_t* fname = wcsrchr( fullPath, L'\\' );
    fname = fname ? fname + 1 : fullPath;

    // Lista de debuggers conhecidos (xorstr_ por string)
    if ( _wcsicmp( fname, xorstr_( L"x64dbg.exe"             ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"x32dbg.exe"             ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"ollydbg.exe"            ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"windbg.exe"             ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"idaq.exe"               ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"idaq64.exe"             ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"ida.exe"                ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"ida64.exe"              ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"ImmunityDebugger.exe"   ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"dnSpy.exe"              ).crypt_get() ) == 0 ) return true;
    if ( _wcsicmp( fname, xorstr_( L"dnSpyEx.exe"            ).crypt_get() ) == 0 ) return true;

    return false;
}
