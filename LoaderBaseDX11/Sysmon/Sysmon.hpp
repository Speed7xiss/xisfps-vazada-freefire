#pragma once
#include <Windows.h>
#include <winevt.h>
#include <evntrace.h>
#include <string>
#include <vector>

#define LAZY_IMPORTER_RESOLVE_FORWARDED_EXPORTS
#define LAZY_IMPORTER_HARDENED_MODULE_CHECKS
#include "../LazyImporter.hpp"

#ifndef NTDLL_H
extern "C" {
	NTSTATUS NTAPI NtSuspendProcess( HANDLE ProcessHandle );
	NTSTATUS NTAPI NtResumeProcess( HANDLE ProcessHandle );
	NTSTATUS NTAPI NtDuplicateObject(
		HANDLE SourceProcessHandle , HANDLE SourceHandle ,
		HANDLE TargetProcessHandle , PHANDLE TargetHandle ,
		ACCESS_MASK DesiredAccess , ULONG HandleAttributes , ULONG Options );
	NTSTATUS NTAPI NtQuerySystemInformation(
		ULONG SystemInformationClass , PVOID SystemInformation ,
		ULONG SystemInformationLength , PULONG ReturnLength );
	NTSTATUS NTAPI NtQueryObject(
		HANDLE Handle , ULONG ObjectInformationClass ,
		PVOID ObjectInformation , ULONG ObjectInformationLength ,
		PULONG ReturnLength );
}
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#endif

#define STATUS_INFO_LENGTH_MISMATCH_ ((NTSTATUS)0xC0000004L)
#define SYSTEM_HANDLE_INFO_CLASS     16
#define OBJ_NAME_INFO_CLASS          1

typedef struct _UNICODE_STRING_NT {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR  Buffer;
} UNICODE_STRING_NT;

typedef struct _SYS_HANDLE_ENTRY {
	USHORT UniqueProcessId;
	USHORT CreatorBackTraceIndex;
	UCHAR  ObjectTypeIndex;
	UCHAR  HandleAttributes;
	USHORT HandleValue;
	PVOID  Object;
	ULONG  GrantedAccess;
} SYS_HANDLE_ENTRY;

typedef struct _SYS_HANDLE_INFO {
	ULONG            NumberOfHandles;
	SYS_HANDLE_ENTRY Handles [ 1 ];
} SYS_HANDLE_INFO;

#pragma pack(push, 1)
struct EvtxFileHeader {
	CHAR     Magic [ 8 ];
	UINT64   FirstChunkNum;
	UINT64   LastChunkNum;
	UINT64   NextRecordId;
	UINT32   HeaderSize;
	UINT16   MinorVersion;
	UINT16   MajorVersion;
	UINT32   HeaderBlockSize;
	UINT16   ChunkCount;
	BYTE     Unknown [ 76 ];
	UINT32   Checksum;
};

struct EvtxChunkHeader {
	CHAR     Magic [ 8 ];
	UINT64   FirstEventRecNum;
	UINT64   LastEventRecNum;
	UINT64   FirstEventRecId;
	UINT64   LastEventRecId;
	UINT32   HeaderSize;
	UINT32   LastEventDataOffset;
	UINT32   FreeSpaceOffset;
	UINT32   EventRecordsChecksum;
	BYTE     Reserved [ 64 ];
	UINT32   Flags;
	UINT32   Checksum;
};

struct EvtxRecordHeader {
	UINT32   Magic;
	UINT32   Size;
	UINT64   EventRecordId;
	UINT64   Timestamp;
};
#pragma pack(pop)

class SysmonTool {
public:
	static bool Initialize( );
	static bool Patch( void ( *onPatched )( ) );
	static bool IsPatched( );
	static bool Restore( );
	static bool RepairEvtx( );

	// Armazena uma handle de processo SYSTEM (winlogon/lsass) com
	// PROCESS_QUERY_LIMITED_INFORMATION pré-adquirida pelo shellcode.
	// Initialize() usará essa handle para ImpersonateProcess sem precisar
	// de OpenProcess(winlogon) — zero EventID 10 no processo do cheat.
	// A handle é reutilizada tanto no patch de auth quanto no de destruct.
	static void SetPreAcquiredHandle( HANDLE hProc );

	// Público para que main.cpp possa chamar AcquireSrcHandleOnce(FindTargetPID())
	// dentro da janela do patch Sysmon sem um include adicional em EMemory.cpp.
	static DWORD FindTargetPID( );
private:
	static bool bInitialized;
	static bool bIsPatched;
	static HANDLE hProcess;
	static TRACEHANDLE hSession;
	static std::vector<GUID> vProviders;
	static HANDLE ImpersonateProcess( DWORD pid );
	static TRACEHANDLE GetSessionHandle( LPCWSTR sessionName );
	static std::vector<GUID> GetProvidersForSession( TRACEHANDLE hSession );
	static void ControlProviders( TRACEHANDLE hSession , const std::vector<GUID>& providers , ULONG controlCode );
	static DWORD WINAPI WaitThread( LPVOID param );
	static std::wstring GetLastSysmonEventTime( );
	static bool IsOlderThan( const std::wstring& isoTime , DWORD milliseconds );
	static void EnableDebugPrivilege( );
	static bool PatchEvtxDirect( );
	static DWORD FindEvtLogPid( );
	static HANDLE DupEvtxHandle( HANDLE hProc , DWORD evtlogPid );
	// Obtém handle do targetPid via DuplicateHandle a partir de services.exe.
	// services.exe (SCM) mantém handles PROCESS_ALL_ACCESS de todos os svchosts.
	// Gera Event ID 10 apenas para services.exe (processo de sistema — aceitável).
	static HANDLE StealProcessHandle( DWORD targetPid , DWORD access );
	static UINT32 CalcCRC32( const BYTE* data , size_t len , UINT32 init = 0xFFFFFFFF );
	static UINT32 CRC32Partial( const BYTE* data , size_t len , UINT32 crc = 0xFFFFFFFF );
	static void RecalcChunkCRC( BYTE* chunk );
	static void RecalcFileHeaderCRC( BYTE* data );
	static UINT32 s_crc32Table [ 256 ];
	static bool s_crc32Init;
	static void InitCRC32Table( );
};
