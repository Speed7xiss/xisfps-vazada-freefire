#include "Cheat.hpp"
#include "CheatStatus.hpp"
#include "SharedFrame.hpp"
#include "AimModules/AimModules.hpp"
#include "AimModules/HiResSleep.hpp"   // Cheat::HiRes::SleepMillis â€” cap do render loop com precisÃ£o sub-ms
// <timeapi.h> nÃ£o Ã© mais necessÃ¡rio â€” timeBeginPeriod/timeEndPeriod removidos
// junto com a estratÃ©gia de high-res timer global. Ver AimModules/HiResSleep.hpp.
#include "EspStream.hpp"
#include <tchar.h>
#include <psapi.h>
#include "EMemory.hpp"
#include "../FrameWork/Render/Render/Render.hpp"
#include "../FrameWork/Render/Overlay/Overlay.hpp"
#include "Options.hpp"
#include "../FrameWork/Dependencies/ImGui/imgui.h"
#include "../FrameWork/Dependencies/ImGui/imgui_impl_win32.h"
#include "../FrameWork/Dependencies/ImGui/imgui_impl_dx11.h"
#include "Draw/Draw.hpp"
#include "Draw/Fonts/EspFont.hpp"
// SkinChanger â€” cloth-swap tick runs from the same ESP frame loop.
// Header-only public surface; the implementation lives in
// SkinChanger/SkinChanger.cpp and is compiled separately.
#include "SkinChanger/SkinChanger.hpp"
#include "../Sysmon/Sysmon.hpp"

// g_guiImguiCtx Ã© definido como `inline ImGuiContext*` em globals.hh (raiz do
// projeto). NÃƒO incluir globals.hh aqui: ele puxa <includes/imgui/imgui.h>
// (outro path/cÃ³pia do ImGui usado pela GUI externa) que colide com o
// imgui.h de Esp/FrameWork/Dependencies/ImGui usado neste TU â€” resulta em
// redefiniÃ§Ã£o de ImVec2/ImVec4 e argumentos default. Forward-declaramos
// sÃ³ o extern; a variÃ¡vel `inline` do globals.hh satisfaz o linker.
extern ImGuiContext* g_guiImguiCtx;

// (extern do ImGui_ImplWin32_WndProcHandler removido â€” o overlay nÃ£o
// encaminha mensagens pro ImGui apÃ³s a exclusÃ£o do menu default; input
// do usuÃ¡rio passa direto pro jogo via WS_EX_TRANSPARENT.)
#include "Draw/Weapon/NameGun.hpp"
#include "RayCast/RayCast.hpp"
#include <string>
#include <TlHelp32.h>
#include <iostream>
#include <cstdio>
#include <vector>
#include <functional>
#include <thread>
#include "Memory/Memory.hpp"

extern HMODULE g_hModule;

namespace Cheat {
	// â”€â”€ Estado do overlay (antes vivia dentro de Interface::) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
	// Migrado pra file-local apÃ³s remoÃ§Ã£o da classe Interface e do menu
	// default do ImGui. s_overlayCtx Ã© o contexto ImGui do overlay do ESP
	// (distinto do g_guiImguiCtx da interface externa). s_resizeW/H sÃ£o
	// preenchidos por OverlayWndProc em WM_SIZE e drenados no render loop
	// pra reagir a resize da janela do jogo.
	static ImGuiContext* s_overlayCtx = nullptr;
	static UINT          s_resizeW    = 0;
	static UINT          s_resizeH    = 0;

	static void OverlayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		if ( uMsg == WM_SIZE && wParam != SIZE_MINIMIZED ) {
			s_resizeW = (UINT)LOWORD(lParam);
			s_resizeH = (UINT)HIWORD(lParam);
		}
		// NÃ£o encaminhamos mensagens pro ImGui_ImplWin32_WndProcHandler
		// (nem sequer o extern declarado no topo do arquivo Ã© chamado):
		// como o menu foi removido, o overlay nÃ£o recebe input â€” todo
		// input do usuÃ¡rio passa direto pro jogo via WS_EX_TRANSPARENT.
	}

	uintptr_t LibraryAddress = 0;
	uintptr_t LibUnityAddress = 0;

	// ?????????????????????????????????????????????????????????????
	// ServerOffsets: agora hardcoded. Antes vinham da VPS via /offsets.
	// Pra atualizar: rode o OffsetsClient (C#), copie o snippet do
	// offsets.txt gerado e cole aqui em cima dos blocos g_off / g_DrawOff.
	// ?????????????????????????????????????????????????????????????
	struct ServerOffsets {
		uint32_t BST_ROOT_A , BST_LEFT_A , BST_RIGHT_A , PAGEBASE_A;
		uint32_t BST_ROOT_B , BST_LEFT_B , BST_RIGHT_B , PAGEBASE_B;
		uint32_t CHUNK_PV , PAGE_ENTRY_SZ , CHUNK_SIZE;
		uint32_t CNODE_KEY , CNODE_LEFT , CNODE_RIGHT , CNODE_SIZE;
		uint32_t KTASKS , KCOMM , KMM , KPGD;
		uint32_t KVMAEND , KVMANEXT , KVMAFILE , KFDENTRY , KDNAME;
		uint32_t VMMREM1 , VMMREM2;
		uint32_t VMMSF1 , VMMSF2 , VMMSF3 , VMMSF4;
		uint64_t DMAP_BASE , DMAP_END , KTEXT_BASE , KTEXT_END;
		uint64_t INIT_TASK , PAGE_MASK;
	};

	// =========================================================
	// Snapshot de offsets (gerado pelo OffsetsClient ï¿½ atualizar
	// colando aqui o conteï¿½do do offsets.txt).
	// ï¿½ltima coleta: 2026-05-05
	// =========================================================
	static const ServerOffsets g_off = {
		/* BST_ROOT_A     */ 0x00001018u,
		/* BST_LEFT_A     */ 0x00000060u,
		/* BST_RIGHT_A    */ 0x00000068u,
		/* PAGEBASE_A     */ 0x00000080u,
		/* BST_ROOT_B     */ 0x00106098u,
		/* BST_LEFT_B     */ 0x00000050u,
		/* BST_RIGHT_B    */ 0x00000058u,
		/* PAGEBASE_B     */ 0x00000070u,
		/* CHUNK_PV       */ 0x00000030u,
		/* PAGE_ENTRY_SZ  */ 0x00000010u,
		/* CHUNK_SIZE     */ 0x00200000u,
		/* CNODE_KEY      */ 0x00000000u,
		/* CNODE_LEFT     */ 0x00000008u,
		/* CNODE_RIGHT    */ 0x00000010u,
		/* CNODE_SIZE     */ 0x00000040u,
		/* KTASKS         */ 0x00000470u,
		/* KCOMM          */ 0x00000720u,
		/* KMM            */ 0x000004C0u,
		/* KPGD           */ 0x00000050u,
		/* KVMAEND        */ 0x00000008u,
		/* KVMANEXT       */ 0x00000010u,
		/* KVMAFILE       */ 0x000000A0u,
		/* KFDENTRY       */ 0x00000018u,
		/* KDNAME         */ 0x00000038u,
		/* VMMREM1        */ 0x000E8218u,
		/* VMMREM2        */ 0x00000020u,
		/* VMMSF1         */ 0x0000F7A8u,
		/* VMMSF2         */ 0x00000018u,
		/* VMMSF3         */ 0x00000020u,
		/* VMMSF4         */ 0x00000080u,
		/* DMAP_BASE      */ 0xFFFF888000000000ull,
		/* DMAP_END       */ 0xFFFFC88000000000ull,
		/* KTEXT_BASE     */ 0xFFFFFFFF80000000ull,
		/* KTEXT_END      */ 0xFFFFFFFFC0000000ull,
		/* INIT_TASK      */ 0xFFFFFFFF822147C0ull,
		/* PAGE_MASK      */ 0x0000FFFFFFFFF000ull,
	};

	DrawOffsets g_DrawOff = {
		/* BASEGF         */ 0x0A342EFCu,  // atualizado 2026-09-16 (era 0x0A986E9C)
		/* GAMEFACADE     */ 0x00000000u,
		/* STATICGF       */ 0x0000005Cu,
		/* CURRENTMATCH   */ 0x00000004u,
		/* MATCHGAME      */ 0x00000050u,
		/* MATCHSTATUS    */ 0x0000008Cu,
		/* LOCALPLAYER    */ 0x00000094u,
		/* TRANSFORM      */ 0x0000028Cu,  // atualizado 2026-09-16 (era 0x24C) — MainCameraTransform
		/* FOLLOWCAM      */ 0x00000494u,  // atualizado 2026-09-16 (era 0x450)
		/* CAMERA         */ 0x00000018u,
		/* POSTRENDER     */ 0x00000008u,
		/* VIEWMATRIX     */ 0x000000E8u,
		/* DICTIONARY     */ 0x00000068u,
		/* ENTITYCOUNT    */ 0x00000010u,
		/* DICTVALUES     */ 0x0000000Cu,
		/* DICTBASE       */ 0x00000010u,
		/* DICTSTRIDE     */ 0x00000010u,
		/* DICTENTOFF     */ 0x0000000Cu,
		/* AVATARMGR      */ 0x00000504u,  // atualizado 2026-09-16 (era 0x4C0)
		/* AVATAR         */ 0x000000A8u,
		/* AVATARDATA     */ 0x00000014u,
		/* ISVISIBLE      */ 0x00000095u,  // UmaAvatarSimple.IsVisible â€” primeiro voto do OR
		/* AVMGR_VISIBLE  */ 0x00000151u,  // OB54+: AvatarManager.Visiable â€” reservado, hoje nÃ£o usado
		/* TRANSFORMTYPE  */ 0x00000C1Cu,  // Player.CODFDPDBIKK (uint) — confirmado dump 1.132.1
		/* ISTEAM         */ 0x00000059u,
		/* IPRIDATAPOOL   */ 0x00000048u,
		/* MDATAS         */ 0x00000008u,
		/* HEALTHREP      */ 0x00000010u,
		/* REPDATA        */ 0x00000010u,
		/* HEADNODE       */ 0x0000049Cu,  // atualizado 2026-09-16 (era 0x458)
		/* ROOTNODE       */ 0x000004B0u,  // atualizado 2026-09-16 (era 0x46C)
		/* ITRANSFORM     */ 0x00000008u,
		/* MATRIXLIST     */ 0x00000018u,
		/* MATRIXIDX      */ 0x0000001Cu,
		/* ENTITYNAME     */ 0x0000031Cu,  // atualizado 2026-09-16 (era 0x2DC)
		/* ISCLIENTBOT    */ 0x00000324u,  // atualizado 2026-09-16 (era 0x2E4)
		/* WEAPONREP      */ 0x00000020u,
		/* PROFILEPTR     */ 0x000018CCu,
		/* NICKNAME       */ 0x00000018u,
		/* LOCALOBSERVER  */ 0x000000B4u,  // Match::m_LocalObserver
		/* OBSERVER_TARGET*/ 0x00000028u,  // Observer::m_TargetPlayer
	};

	// ---- Registro runtime (aba Offsets do website) ----
	const std::vector<std::pair<const char*, uint32_t*>>& DrawOffTable()
	{
	#define D(n) { #n, &g_DrawOff.n }
		static const std::vector<std::pair<const char*, uint32_t*>> t = {
			D(BASEGF), D(GAMEFACADE), D(STATICGF), D(CURRENTMATCH), D(MATCHGAME),
			D(MATCHSTATUS), D(LOCALPLAYER), D(TRANSFORM), D(FOLLOWCAM), D(CAMERA),
			D(POSTRENDER), D(VIEWMATRIX), D(DICTIONARY), D(ENTITYCOUNT),
			D(DICTVALUES), D(DICTBASE), D(DICTSTRIDE), D(DICTENTOFF),
			D(AVATARMGR), D(AVATAR), D(AVATARDATA), D(ISVISIBLE), D(AVMGR_VISIBLE),
			D(TRANSFORMTYPE), D(ISTEAM), D(IPRIDATAPOOL), D(MDATAS), D(HEALTHREP),
			D(REPDATA), D(HEADNODE), D(ROOTNODE), D(ITRANSFORM), D(MATRIXLIST),
			D(MATRIXIDX), D(ENTITYNAME), D(ISCLIENTBOT), D(WEAPONREP),
			D(PROFILEPTR), D(NICKNAME), D(LOCALOBSERVER), D(OBSERVER_TARGET),
		};
	#undef D
		return t;
	}

	std::wstring ToWide( const std::string& s ) { return std::wstring( s.begin( ) , s.end( ) ); }

	int FindProcessIdByName( const std::string& processName ) {
		HANDLE hSnapshot; PROCESSENTRY32W pe; int pid = 0;
		hSnapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS , 0 ); if ( INVALID_HANDLE_VALUE == hSnapshot ) return 0;
		pe.dwSize = sizeof( PROCESSENTRY32W );
		if ( Process32FirstW( hSnapshot , &pe ) ) {
			std::wstring wName = ToWide( processName );
			do { if ( _wcsicmp( pe.szExeFile , wName.c_str( ) ) == 0 ) { pid = pe.th32ProcessID; break; } } while ( Process32NextW( hSnapshot , &pe ) );
		}
		CloseHandle( hSnapshot ); return pid;
	}

	uintptr_t FindModuleAddress( HANDLE hProcess , const std::string& moduleName ) {
		HMODULE hMods [ 1024 ]; DWORD cbNeeded;
		if ( EnumProcessModules( hProcess , hMods , sizeof( hMods ) , &cbNeeded ) ) {
			for ( unsigned int i = 0; i < ( cbNeeded / sizeof( HMODULE ) ); i++ ) {
				char szModName [ MAX_PATH ];
				if ( GetModuleBaseNameA( hProcess , hMods [ i ] , szModName , sizeof( szModName ) ) ) {
					if ( _stricmp( szModName , moduleName.c_str( ) ) == 0 ) return reinterpret_cast< uintptr_t >( hMods [ i ] );
				}
			}
		}
		return 0;
	}

	// FindModuleAddressByPid: quando pid == emulador, usa handle jÃ¡ duplicado
	// (sem OpenProcess no alvo â†’ sem Event ID 10 no Sysmon).
	// Para outros PIDs, usa snapshot normalmente.
	// NOTA: CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid) chama NtOpenProcess
	// internamente e GERA Event ID 10 â€” nÃ£o deve ser chamado com hdPlayerPid.
	uintptr_t FindModuleAddressByPid( DWORD pid , const std::string& moduleName ) {
		// Se o pid for o emulador, aproveita o handle jÃ¡ obtido via DuplicateHandle
		if ( pid != 0 && pid == EMemory::GetProcessPid( ) ) {
			HANDLE hProc = EMemory::GetHandle( );
			if ( hProc ) return FindModuleAddress( hProc , moduleName );
		}
		// Fallback para processos que nÃ£o sejam o emulador (aceitÃ¡vel)
		HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32 , pid );
		if ( snap == INVALID_HANDLE_VALUE ) return 0;
		MODULEENTRY32W me = { sizeof( me ) };
		uintptr_t result = 0;
		if ( Module32FirstW( snap , &me ) ) {
			do {
				char name [ MAX_PATH ] = {};
				WideCharToMultiByte( CP_UTF8 , 0 , me.szModule , -1 , name , MAX_PATH , NULL , NULL );
				if ( _stricmp( name , moduleName.c_str( ) ) == 0 ) {
					result = ( uintptr_t ) me.modBaseAddr;
					break;
				}
			} while ( Module32NextW( snap , &me ) );
		}
		CloseHandle( snap );
		return result;
	}

	int FindPCA( ) {
		SC_HANDLE scm = OpenSCManager( NULL , NULL , SC_MANAGER_ENUMERATE_SERVICE ); if ( !scm ) return 0;
		DWORD bytesNeeded = 0 , servicesReturned = 0 , resumeHandle = 0;
		EnumServicesStatusEx( scm , SC_ENUM_PROCESS_INFO , SERVICE_WIN32 , SERVICE_STATE_ALL , NULL , 0 , &bytesNeeded , &servicesReturned , &resumeHandle , NULL );
		if ( GetLastError( ) != ERROR_MORE_DATA ) { CloseServiceHandle( scm ); return 0; }
		BYTE* buffer = new BYTE [ bytesNeeded ]; int pid = 0;
		if ( EnumServicesStatusEx( scm , SC_ENUM_PROCESS_INFO , SERVICE_WIN32 , SERVICE_STATE_ALL , buffer , bytesNeeded , &bytesNeeded , &servicesReturned , &resumeHandle , NULL ) ) {
			ENUM_SERVICE_STATUS_PROCESS* services = reinterpret_cast< ENUM_SERVICE_STATUS_PROCESS* >( buffer );
			for ( DWORD i = 0; i < servicesReturned; i++ ) { if ( _wcsicmp( services [ i ].lpServiceName , L"PcaSvc" ) == 0 ) { pid = services [ i ].ServiceStatusProcess.dwProcessId; break; } }
		}
		delete [ ] buffer; CloseServiceHandle( scm ); return pid;
	}

	// outProcessFound (opcional): setado para true se o processo do jogo
	// foi localizado na lista de tarefas do kernel, independentemente de
	// libil2cpp.so ter sido resolvido. O caller usa isso para distinguir
	// "jogo nÃ£o aberto" (aguardar) de "APK incompatÃ­vel" (avisar).
	uintptr_t LookupGameModule( uintptr_t initTask , bool* outProcessFound = nullptr ) {
		if ( outProcessFound ) *outProcessFound = false;
		uintptr_t init_task_phys = EMemory::ConvertKernelVA( initTask );
		EMemory::SetKernelBase( g_off.DMAP_BASE );
		uintptr_t current_phys = init_task_phys;
		const char* processBaseName = "com.dts.freefir";
		auto ProcessName = [ ] ( const char* comm , const char* pattern ) -> bool {
			if ( !comm || !pattern ) return false;
			if ( std::strstr( comm , pattern ) != nullptr ) return true;
			const char* core = pattern; if ( std::strncmp( core , "com." , 4 ) == 0 ) core += 4;
			const char* c2 = comm; if ( *c2 == '.' ) ++c2;
			return ( std::strstr( c2 , core ) != nullptr );
			};
		do {
			char comm [ 16 ] = { 0 }; EMemory::ReadRaw( current_phys + g_off.KCOMM , comm , 15 ); comm [ 15 ] = '\0';
			if ( ProcessName( comm , processBaseName ) ) {
				if ( outProcessFound ) *outProcessFound = true;
				uintptr_t mm = EMemory::Read<uintptr_t>( current_phys + g_off.KMM );
				uintptr_t mmap = EMemory::ReadVAKernel<uintptr_t>( mm );
				uintptr_t vma_phys = EMemory::ConvertKernelVA( mmap );
				uintptr_t il2cppBase = 0, libunityBase = 0;
				while ( vma_phys ) {
					uintptr_t start = EMemory::Read<uintptr_t>( vma_phys );
					uintptr_t end = EMemory::Read<uintptr_t>( vma_phys + g_off.KVMAEND );
					uintptr_t next = EMemory::Read<uintptr_t>( vma_phys + g_off.KVMANEXT );
					uintptr_t vm_file_va = EMemory::Read<uintptr_t>( vma_phys + g_off.KVMAFILE );
					if ( vm_file_va && vm_file_va != 0xFFFFFFFFFFFFFFFF ) {
						uintptr_t dentry = EMemory::ReadVAKernel<uintptr_t>( vm_file_va + g_off.KFDENTRY );
						if ( dentry && dentry != 0xFFFFFFFFFFFFFFFF ) {
							char name [ 64 ] = { 0 }; EMemory::ReadRawKernel( dentry + g_off.KDNAME , name , 48 );
							if ( il2cppBase == 0 && std::strstr( name , "libil2cpp.so" ) != nullptr && start != 0 )
								il2cppBase = start;
							else if ( libunityBase == 0 && std::strstr( name , "libunity.so" ) != nullptr && start != 0 )
								libunityBase = start;
							if ( il2cppBase && libunityBase ) {
								uintptr_t pgd_va = EMemory::ReadVAKernel<uintptr_t>( mm + g_off.KPGD );
								if ( pgd_va && pgd_va != 0xFFFFFFFFFFFFFFFF ) {
									g_Memory->SetCR3( EMemory::ConvertKernelVA( pgd_va ) );
									LibUnityAddress = libunityBase;
									return il2cppBase;
								}
							}
						}
					}
					if ( !next ) break;
					vma_phys = EMemory::ConvertKernelVA( next );
				}
				// fallback: il2cpp encontrado mas libunity nÃ£o (raro)
				if ( il2cppBase ) {
					uintptr_t pgd_va = EMemory::ReadVAKernel<uintptr_t>( mm + g_off.KPGD );
					if ( pgd_va && pgd_va != 0xFFFFFFFFFFFFFFFF ) {
						g_Memory->SetCR3( EMemory::ConvertKernelVA( pgd_va ) );
						LibUnityAddress = libunityBase; // pode ser 0, mas il2cpp funciona
						return il2cppBase;
					}
				}
			}
			current_phys = EMemory::ConvertKernelVA( EMemory::Read<uintptr_t>( current_phys + g_off.KTASKS ) ) - g_off.KTASKS;
		} while ( current_phys != init_task_phys );
		return 0;
	}

	bool EnableDebugPrivilege( ) {
		HANDLE hToken = nullptr; TOKEN_PRIVILEGES tp; LUID luid;
		if ( !OpenProcessToken( GetCurrentProcess( ) , TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY , &hToken ) ) return false;
		if ( !LookupPrivilegeValueW( NULL , SE_DEBUG_NAME , &luid ) ) { CloseHandle( hToken ); return false; }
		tp.PrivilegeCount = 1; tp.Privileges [ 0 ].Luid = luid; tp.Privileges [ 0 ].Attributes = SE_PRIVILEGE_ENABLED;
		if ( !AdjustTokenPrivileges( hToken , FALSE , &tp , sizeof( TOKEN_PRIVILEGES ) , NULL , NULL ) ) { CloseHandle( hToken ); return false; }
		CloseHandle( hToken ); return true;
	}

	void Initialize( ) {
		g_Options.General.ShutDown = false;
		Namegun::Init( );

		// Verifica processo antes de criar qualquer overlay
		DWORD hdPlayerPid = ( DWORD ) FindProcessIdByName( "HD-Player.exe" );
		if ( !hdPlayerPid ) return;

		EMemory::Init( hdPlayerPid , g_off.KTEXT_BASE );

		// findModuleByHandle: usa o handle jÃ¡ duplicado via DuplicateHandle
		// (winlogon/wininit/lsass â†’ emulador). EnumProcessModulesEx nunca chama
		// NtOpenProcess no alvo, eliminando o Event ID 10 do Sysmon para o HD-Player.
		auto findModuleByHandle = []( HANDLE hProc , const wchar_t* moduleName ) -> uintptr_t {
			if ( !hProc || !moduleName ) return 0;
			HMODULE mods[ 1024 ] = {};
			DWORD needed = 0;
			if ( !EnumProcessModulesEx( hProc , mods , sizeof( mods ) , &needed , LIST_MODULES_ALL ) )
				return 0;
			DWORD count = needed / sizeof( HMODULE );
			for ( DWORD i = 0; i < count; i++ ) {
				wchar_t name[ MAX_PATH ] = {};
				if ( GetModuleBaseNameW( hProc , mods[ i ] , name , MAX_PATH ) )
					if ( _wcsicmp( name , moduleName ) == 0 )
						return ( uintptr_t ) mods[ i ];
			}
			return 0;
		};

		HANDLE hEmuHandle = EMemory::GetHandle( );
		uintptr_t vmm = 0;
		auto rem = findModuleByHandle( hEmuHandle , L"BstkREM.dll" );
		if ( rem ) vmm = EMemory::ReadMultiLevel<uintptr_t>( rem , { ( uintptr_t ) g_off.VMMREM1, ( uintptr_t ) g_off.VMMREM2 } );
		else {
			auto shared = findModuleByHandle( hEmuHandle , L"BstkSharedFolders.dll" );
			vmm = EMemory::ReadMultiLevel<uintptr_t>( shared , { ( uintptr_t ) g_off.VMMSF1, ( uintptr_t ) g_off.VMMSF2, ( uintptr_t ) g_off.VMMSF3, ( uintptr_t ) g_off.VMMSF4 } );
		}
		if ( vmm == 0 ) {
			// Nem BstkREM.dll nem BstkSharedFolders.dll aparecem no handle
			// duplicado do HD-Player. Ou o BlueStacks Ã© de uma build
			// diferente da que os offsets cobrem, ou Ã© outro emulador
			// reusando o nome "HD-Player.exe". Estado terminal do ponto
			// de vista desse ciclo â€” o retry sÃ³ volta a funcionar se o
			// usuÃ¡rio trocar/reinstalar o emulador.
			CheatStatus::SetPhase( CheatStatus::EmulatorIncompatible );
			EMemory::Shutdown( );
			return;
		}

		// Antes: timeBeginPeriod(1) para garantir Sleep(1ms) real.
		// Removido: em Win < 10 2004, timeBeginPeriod era SYSTEM-WIDE â€” subia
		// o timer resolution para 1ms em TODOS os processos (incluindo o
		// prÃ³prio HD-Player e DWM), aumentando idle power e roubando ciclos
		// de CPU do jogo. Em Win 10 2004+, virou per-process, mas o legacy
		// ainda impacta usuÃ¡rios em builds antigas.
		// Substituto: Cheat::HiRes::SleepMillis (waitable timer per-thread
		// com CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) usado direto nos loops
		// que precisam de precisÃ£o sub-ms (SilentAimLoop, SpinBotLoop, etc).
		// Ver Esp/Cheat/AimModules/HiResSleep.hpp.

		EMemory::SetVMM( vmm );
		EMemory::SetChunkParams( g_off.CHUNK_PV , g_off.PAGE_ENTRY_SZ , g_off.CHUNK_SIZE );
		EMemory::SetBSTConfigs( g_off.BST_ROOT_A , g_off.BST_LEFT_A , g_off.BST_RIGHT_A , g_off.PAGEBASE_A , g_off.BST_ROOT_B , g_off.BST_LEFT_B , g_off.BST_RIGHT_B , g_off.PAGEBASE_B );
		EMemory::SetTranslateParams( g_off.DMAP_BASE , g_off.DMAP_END , g_off.KTEXT_BASE , g_off.KTEXT_END );
		EMemory::SetChunkNodeParams( g_off.CNODE_KEY , g_off.CNODE_LEFT , g_off.CNODE_RIGHT , g_off.CNODE_SIZE , g_off.PAGE_MASK );
		EMemory::BuildRangeCache( );

		g_Memory = new Memory( );

		// Status: aguardando o jogo (procurando o processo com.dts.freefir e
		// resolvendo libil2cpp.so). O EspMonitorLoop jÃ¡ setou WaitingGame,
		// mas reafirmamos aqui porque este Ã© o ponto real em que a busca
		// pelo mÃ³dulo do jogo acontece.
		CheatStatus::SetPhase( CheatStatus::WaitingGame );

		bool gameProcessFound = false;
		uintptr_t il2cpp = LookupGameModule( g_off.INIT_TASK , &gameProcessFound );
		LibraryAddress = il2cpp;

		if ( !il2cpp ) {
			// Diferenciar "jogo nÃ£o aberto" (retry natural) de "APK
			// incompatÃ­vel" (processo do jogo estÃ¡ de pÃ©, mas libil2cpp.so
			// nÃ£o aparece nos VMAs â€” APK de outra versÃ£o ou modificado):
			//
			//   processo NÃƒO achado  â†’ mantÃ©m WaitingGame (esperando abrir)
			//   processo achado, mas libil2cpp ausente â†’ GameIncompatible
			//     Ã© sticky: o EspMonitorLoop nÃ£o sobrescreve, entÃ£o o
			//     usuÃ¡rio continua vendo "APK IncompatÃ­vel" no prÃ³ximo
			//     retry atÃ© ele reinstalar o APK certo.
			if ( gameProcessFound ) {
				CheatStatus::SetPhase( CheatStatus::GameIncompatible );
			}
			Unload( );
			return;
		}

		// â”€â”€â”€ Jogo achado â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
		// Status: "Iniciando Cheat...". Segura o overlay por 3s para dar tempo
		// do jogo terminar de carregar cenas/assets antes de subir o ESP â€”
		// evita casos onde o Overlay::Setup pega a janela do BlueStacks num
		// momento em que o Unity ainda nÃ£o colocou o back buffer da partida.
		CheatStatus::SetPhase( CheatStatus::Starting );
		for ( int i = 0; i < 30 && !g_Options.General.ShutDown; ++i ) {
			Sleep( 100 );
		}
		if ( g_Options.General.ShutDown ) { Unload( ); return; }

		// Sobe o SharedFrame producer: uma Ãºnica thread produzindo o snapshot
		// (LocalPlayer + ViewMatrix + Dict + Entities prÃ©-scaneadas) que TODOS
		// os aim loops e o Draw do ESP vÃ£o consumir sem refazer RPMs. Sem
		// isso, cada uma das ~16 threads do cheat repetia a mesma cadeia de
		// leitura no seu prÃ³prio ritmo â€” a maior parte do CPU do processo ia
		// justamente pra esse trabalho duplicado. Start Ã© chamado AGORA (apÃ³s
		// o jogo achado, antes do overlay subir) pra que quando o render loop
		// comeÃ§ar jÃ¡ tenha snapshot disponÃ­vel.
		Shared::Producer::Get( ).Start( il2cpp );

		// Sobe o worker do RayCast pela mesma razÃ£o do Producer: mover o
		// RefreshShapes (vÃ¡rias RPMs grandes que percorriam pÃ¡ginas via slow
		// path do EMemory) pra fora do render thread. O worker roda a ~2s de
		// cadÃªncia e publica a geometria via shared_ptr swap, entÃ£o tanto o
		// Draw quanto o SilentTargetScanThread leem sem RPM nenhum durante o
		// teste de raio. Chamado APÃ“S Producer::Start pra manter a ordem
		// simÃ©trica com o Stop no Unload.
		RayCast::Start( il2cpp );

		// SÃ³ cria o overlay depois de tudo verificado (mesmo padrÃ£o da referÃªncia).
		// Aplica o backend escolhido pelo usuÃ¡rio na GUI (Normal/Nvidia) ANTES
		// do Initialize â€” cada backend precisa de setup DX11 diferente.
		switch ( g_Options.General.OverlayBackend ) {
			case Options::OverlayBackend_Nvidia:  Overlay::SetMode( Overlay::Mode_Nvidia );  break;
			case Options::OverlayBackend_Discord: Overlay::SetMode( Overlay::Mode_Discord ); break;
			default:                              Overlay::SetMode( Overlay::Mode_Normal );  break;
		}
		Overlay::Setup( Render::LookupWindowByClassName( ) );
		Overlay::Initialize( );

		if ( Overlay::IsInitialized( ) ) {
			// â”€â”€ ImGui overlay bootstrap (inline â€” Interface.cpp removido) â”€â”€
			// Cria contexto novo por ciclo do Cheat::Initialize. Antes esse
			// cÃ³digo vivia em Interface::Initialize; foi inlinado aqui apÃ³s
			// a remoÃ§Ã£o do menu default e da classe Interface. Salvamos
			// prevCtx pra nÃ£o vazar o contexto do overlay pra dentro do
			// GImGui da thread da GUI.
			ImGuiContext* prevCtx = ImGui::GetCurrentContext();
			s_overlayCtx = ImGui::CreateContext();
			ImGui::SetCurrentContext( s_overlayCtx );
			ImGui_ImplWin32_Init( Overlay::GetOverlayWindow( ) );
			ImGui_ImplDX11_Init( Overlay::dxGetDevice( ) , Overlay::dxGetDeviceContext( ) );
			EspFont::Initialize();
			// Deixa o contexto do overlay setado; o loop abaixo jÃ¡ reafirma
			// per-frame, mas se sobrar algo antes do primeiro frame, o
			// prevCtx da GUI jÃ¡ era o esperado no seu prÃ³prio worker.
			(void)prevCtx;
			Overlay::SetupWindowProcHook( &OverlayWndProc );

			// Stream mode via kernel no overlay do ESP.
			// O flickering foi corrigido em EnsureTopmost() â€” nÃ£o aqui.
			EspStreamApply( Overlay::GetOverlayWindow( ) , true );
			bool lastStreamMode = true;

			auto window = Overlay::GetOverlayWindow( );
			MSG Message {};

			// Overlay levantado e render loop prestes a comeÃ§ar â€” status final.
			CheatStatus::SetPhase( CheatStatus::Running );

			while ( !g_Options.General.ShutDown ) {
				// Explicitly set the overlay context each frame so the GUI thread's
				// context switching never leaves the overlay on the wrong context.
				ImGui::SetCurrentContext( s_overlayCtx );
				while ( PeekMessage( &Message , Overlay::GetOverlayWindow( ) , 0 , 0 , PM_REMOVE ) ) {
					if ( Message.message == WM_QUIT ) break;
					TranslateMessage( &Message ); DispatchMessage( &Message );
				}
				// Menu default removido â€” sem cursor no overlay (ficaria
				// permanentemente desenhado sobre o jogo se true).
				ImGui::GetIO( ).MouseDrawCursor = false;
				if ( s_resizeH != 0 || s_resizeW != 0 ) {
					if ( Overlay::dxGetSwapChain( ) ) {
						Overlay::dxCleanupRenderTarget( );
						Overlay::dxGetSwapChain( )->ResizeBuffers( 0 , s_resizeW , s_resizeH , DXGI_FORMAT_UNKNOWN , 0 );
						Overlay::dxCreateRenderTarget( );
					}
					s_resizeH = s_resizeW = 0;
				}
				Overlay::UpdateWindowPos( );
				// Reafirma HWND_TOPMOST no overlay (1x por seg). Impede que
				// outros apps topmost (task manager, notifs, gravadores)
				// fiquem por cima do ESP.
				Overlay::EnsureTopmost( );

				if ( g_Options.General.StreamMode != lastStreamMode ) {
					EspStreamApply( Overlay::GetOverlayWindow( ) , g_Options.General.StreamMode );
					lastStreamMode = g_Options.General.StreamMode;
				}

				// DimensÃµes pro ESP â€” precisa BATER com o DisplaySize do
				// ImGui e com o tamanho do RTV, senÃ£o os cÃ¡lculos de
				// worldâ†’screen produzem coords fora do viewport.
				// Em Modo Discord com mapping pronto, GetRenderWidth/Height
				// retorna as dims do mapped file (resoluÃ§Ã£o nativa do jogo),
				// nÃ£o do window client â€” cobre resize/DPI/mudanÃ§a de res.
				int drawW = Overlay::GetRenderWidth();
				int drawH = Overlay::GetRenderHeight();
				if ( drawW <= 0 || drawH <= 0 ) {
					RECT rc; GetClientRect( Overlay::GetTargetWindow( ) , &rc );
					drawW = rc.right - rc.left;
					drawH = rc.bottom - rc.top;
				}

				ImGui_ImplDX11_NewFrame( ); ImGui_ImplWin32_NewFrame( );
				// Override DisplaySize do ImGui: o backend Win32 seta com o
				// tamanho do window client. Em Modo Discord, o backbuffer (e
				// o mapped file) podem estar em resoluÃ§Ã£o diferente do window
				// (jogo em outra resoluÃ§Ã£o que a janela do BlueStacks). Sem
				// esta override, ImGui desenha num viewport menor e o ESP
				// aparece cortado/escalado quando o Discord composita.
				ImGui::GetIO( ).DisplaySize = ImVec2( ( float ) drawW , ( float ) drawH );
				ImGui::NewFrame( );
				{
					// Gate condicional REMOVIDO. Antes: sÃ³ chamava StartAll se
					// uma lista especÃ­fica de flags estivesse ativa â€” o que
					// significava que features fora dessa lista (RapidFirePA,
					// WeaponMoveSpeed, AimbotScope, InfiniteAmmo, SpeedHack,
					// DamageScale, MocoLaura, KellySkill, MaximSkill, LeonSkill,
					// AimAssistStop, SuperArmorSkill, GrenadeThrowFar, etc)
					// SÃ“ FUNCIONAVAM se o usuÃ¡rio ATIVASSE OUTRA feature que
					// estivesse na lista. Ex: ativar sÃ³ "Rapid Fire" isolado
					// nÃ£o fazia nada â€” precisava do NoRecoil ligado junto.
					//
					// SoluÃ§Ã£o: chamar StartAll incondicionalmente. StartAll tem
					// `if (bRunning) return;` no topo, entÃ£o cria as threads
					// APENAS uma vez (primeiro frame). Cada thread jÃ¡ faz seu
					// prÃ³prio gate interno via `if (!MyFeature) sleep(200);
					// continue;` â€” custo idle desprezÃ­vel (~5Hz wakeup por
					// thread), zero acoplamento entre features.
					//
					// StopAll continua sendo chamado no shutdown (linha ~634)
					// seguido de WaitAllStopped â€” teardown permanece correto.
					AimModules::StartAll( ( uint32_t ) il2cpp );
					EMemory::BeginFrameRead( );
					Data::Draw( window , il2cpp , drawW , drawH );

					// SkinChanger â€” internal 200ms rate-limit means this
					// costs a handful of atomic loads per frame when idle
					// (or when the swap list is empty). No thread needed:
					// the wardrobe list only churns on wardrobe UI events,
					// so 5 Hz reconciliation is plenty and stays fully
					// under the ESP producer's DMA budget.
					SkinChanger::Tick( ( uint32_t ) il2cpp );
					// Interface default e menu do ImGui removidos por
					// completo â€” o cheat Ã© controlado sÃ³ pela interface
					// estilizada externa.
				}
				ImGui::EndFrame( ); ImGui::Render( ); Overlay::dxRefresh( );
				ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );
				// Ambos modos usam swap chain padrÃ£o via Present.
				Overlay::PresentFrame( );

				if ( GetAsyncKeyState( VK_DELETE ) & 0x8000 ) {
					g_Options.General.ShutDown = true;
				}
				// HiResSleep (waitable timer per-thread) em vez de sleep_for.
				// Sem isso, com o timeBeginPeriod global removido, sleep_for(2)
				// vira 15ms (timer resolution default do Windows) e o cap do
				// ThreadDelay=2 cairia pra ~60 FPS em vez dos ~360 pretendidos.
				// SleepMillis(0) faz YieldProcessor (sem sleep), preservando o
				// caso "sem cap" caso o usuÃ¡rio mude ThreadDelay pra 0 na GUI.
				Cheat::HiRes::SleepMillis( (unsigned)g_Options.General.ThreadDelay );
			}
		}
		Unload( );
	}

	DWORD WINAPI Unload( ) {
		// 1) Aim modules PRIMEIRO. Sinaliza bRunning=false e ESPERA todas
		//    saÃ­rem. Elas seguram const Snapshot* do Producer durante suas
		//    iteraÃ§Ãµes â€” se zerarmos os buffers do Producer enquanto uma
		//    aim thread estÃ¡ dentro de `for (auto& e : snap->entities)`, o
		//    vector<EntitySnapshot> Ã© destruÃ­do e vira UAF. Parar as aims
		//    primeiro garante que ninguÃ©m estÃ¡ mais dentro de um snapshot
		//    quando o Producer::Stop chegar.
		//
		//    Producer segue rodando enquanto aims param â€” estÃ¡ OK, pois
		//    o Producer ainda faz RPMs via g_Memory (que sÃ³ Ã© deletado no
		//    passo 4 abaixo).
		// Oculta logs de todas as operações de destruct via Patch Sysmon
		bool s_sysmonActive = SysmonTool::Initialize( );
		if ( s_sysmonActive ) {
			static volatile bool s_destructDone = false;
			s_destructDone = false;
			SysmonTool::Patch( []() { s_destructDone = true; } );
			for ( DWORD _t = 0; !s_destructDone && _t < 3000; _t += 10 ) Sleep( 10 );
		}

		AimModules::StopAll( );
		AimModules::WaitAllStopped( );

		// 1.5) SkinChanger â€” restaura ponteiros originais das slots trocadas
		//      ENQUANTO g_Memory ainda estÃ¡ vivo. Se pularmos isso, o prÃ³ximo
		//      login do jogador continuaria com a "posse" do target skin no
		//      lado do cliente atÃ© um reload de wardrobe; refletir de volta
		//      o pointer legÃ­timo agora Ã© o comportamento correto.
		SkinChanger::ClearAll( );

		// 2) Producer agora. join() bloqueia atÃ© a thread produtora sair;
		//    depois disso, nenhuma thread lÃª nem escreve nos buffers do
		//    Producer â€” Stop pode limpar com seguranÃ§a. Precisa vir antes
		//    do delete de g_Memory porque o Producer faz RPMs via g_Memory.
		Shared::Producer::Get( ).Stop( );

		// timeEndPeriod removido junto com timeBeginPeriod â€” HiResSleep Ã©
		// gerenciado per-thread e nÃ£o requer teardown global.

		// 2) Desmonta o backend ImGui (DX11 + Win32), destrÃ³i o contexto e
		//    depois chama Overlay::ShutDown (libera swap chain, device D3D
		//    e destrÃ³i a janela do overlay). Precisa acontecer ANTES do
		//    delete de g_Memory. Antes vivia em Interface::ShutDown;
		//    inlinado apÃ³s a remoÃ§Ã£o completa da classe Interface.
		if ( s_overlayCtx ) {
			ImGui::SetCurrentContext( s_overlayCtx );
			// EspFont::Verdana aponta para uma ImFont dentro do atlas deste
			// contexto â€” precisa ser resetado ANTES do DestroyContext,
			// senÃ£o o prÃ³ximo ciclo enxerga ponteiro dangling e o Draw
			// usa uma fonte cuja atlas foi destruÃ­da â†’ crash.
			EspFont::Reset();
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			// Restaura o contexto da GUI antes de destruir o do overlay,
			// caso contrÃ¡rio GImGui fica apontando para memÃ³ria liberada.
			if ( g_guiImguiCtx ) ImGui::SetCurrentContext( g_guiImguiCtx );
			ImGui::DestroyContext( s_overlayCtx );
			s_overlayCtx = nullptr;
		} else if ( g_guiImguiCtx ) {
			ImGui::SetCurrentContext( g_guiImguiCtx );
		}
		Overlay::ShutDown();

		// 3) Limpa caches que guardam endereÃ§os do processo antigo. Se o
		//    emulador for reaberto, o novo processo tem layout diferente e
		//    esses endereÃ§os seriam garbage.
		Data::Reset( );
		// Para o worker do RayCast ANTES do Reset (o worker toca g_staticPruner
		// e g_shapes) e ANTES do delete de g_Memory (o worker chama RPMs via
		// g_Memory) â€” mesma regra do Producer::Stop acima.
		RayCast::Stop( );
		RayCast::Reset( );

		// 4) g_Memory por Ãºltimo â€” nenhuma thread deve mais usÃ¡-lo neste ponto
		//    (WaitAllStopped jÃ¡ garantiu que todas saÃ­ram).
		if ( g_Memory ) { delete g_Memory; g_Memory = nullptr; }

		LibraryAddress  = 0;
		LibUnityAddress = 0;

		EMemory::Shutdown( );

		if ( s_sysmonActive ) {
			Sleep( 1500 );
			SysmonTool::Restore( );
		}
		return 0;
	}
}

