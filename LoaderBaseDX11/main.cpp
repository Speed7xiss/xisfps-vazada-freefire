
#define IMGUI_DEFINE_MATH_OPERATORS
#include <includes/imgui/imgui.h>
#include <includes/imgui/imgui_impl_win32.h>
#include <includes/imgui/imgui_impl_dx11.h>
#include <includes/imgui/imgui_internal.h>
#include <includes/imgui/imgui_freetype.h>

#include <globals.hh>
#include "Cheat/EMemory.hpp"
#include "Cheat/CheatManager.hpp"
#include "Sysmon/Sysmon.hpp"
#include "Prefetch/Prefetch.hpp"
#include "Auth/auth.hpp"
#include "XorStr/XorStr.hpp"
#include "Sysmon/Sysmon.hpp"
#include "AntiDbg/AntiDbg.hpp"
#include "Imgui/menu/helpers/debug_log.h"
#include "DeleteSysmon/wipe_logs.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <thread>
#include <chrono>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <tchar.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "freetype.lib")

#ifndef BUILD_AS_EXE
#include <corecrt_startup.h>
extern "C" {
    extern _PVFV __xc_a[];
    extern _PVFV __xc_z[];
}
#pragma comment(linker, "/include:__xc_a")
#pragma comment(linker, "/include:__xc_z")
#endif

// static void DBG(const char* msg) {
//     HANDLE h = CreateFileA("C:\\Users\\teu\\Desktop\\cheat_debug.txt",
//         FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
//         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
//     if (h == INVALID_HANDLE_VALUE) return;
//     SYSTEMTIME t; GetLocalTime(&t);
//     char buf[512];
//     int n = wsprintfA(buf, "[%02d:%02d:%02d.%03d] %s\r\n",
//         t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
//     DWORD w; WriteFile(h, buf, (DWORD)n, &w, nullptr);
//     CloseHandle(h);
// }

namespace GlobalState
{
    bool            g_bRunning   = true;
    ID3D11Device*   g_pd3dDevice = nullptr;
}

extern void setup_menu();
extern void frame_update();
extern void menu();

static ID3D11DeviceContext*     g_pd3dDeviceContext    = nullptr;
static IDXGISwapChain*          g_pSwapChain           = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void DoPrefetchSafe()
{
    __try {
        Prefetch::Get()->PrefetchSystem();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

inline void StartThreadPrefetch()
{
    while (true) {
        DoPrefetchSafe();
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd                        = {};
    sd.BufferCount                                 = 2;
    sd.BufferDesc.Format                           = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator            = 60;
    sd.BufferDesc.RefreshRate.Denominator          = 1;
    sd.Flags                                       = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                                 = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                                = hWnd;
    sd.SampleDesc.Count                            = 1;
    sd.Windowed                                    = TRUE;
    sd.SwapEffect                                  = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevelArray, 2, D3D11_SDK_VERSION,
            &sd, &g_pSwapChain, &GlobalState::g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)              { g_pSwapChain->Release();              g_pSwapChain              = nullptr; }
    if (g_pd3dDeviceContext)       { g_pd3dDeviceContext->Release();       g_pd3dDeviceContext       = nullptr; }
    if (GlobalState::g_pd3dDevice) { GlobalState::g_pd3dDevice->Release(); GlobalState::g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        GlobalState::g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (GlobalState::g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        GlobalState::g_bRunning = false;
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

static void DoRenderFrame()
{
    __try {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        frame_update();
        menu();

        ImGui::Render();

        const float clear[4] = { 0.051f, 0.051f, 0.055f, 1.f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        GlobalState::g_bRunning = false;
    }
}

void IniciarInterface(HINSTANCE hInstance)
{
    constexpr int WIN_W = 720;
    constexpr int WIN_H = 460;

    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"_";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_APPWINDOW,
        wc.lpszClassName, L"arquivo",
        WS_POPUP,
        (GetSystemMetrics(SM_CXSCREEN) - WIN_W) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - WIN_H) / 2,
        WIN_W, WIN_H,
        nullptr, nullptr, hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInstance);
        return;
    }

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    globals.hwnd = hwnd;

    CheatManager::Start();

    std::thread([]() {
        try { StartThreadPrefetch(); } catch (...) {}
    }).detach();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    g_guiImguiCtx = ImGui::GetCurrentContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(GlobalState::g_pd3dDevice, g_pd3dDeviceContext);

    setup_menu();

    MSG msg = {};
    while (GlobalState::g_bRunning && msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }
        DoRenderFrame();
    }

    CheatManager::Stop();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);
}

static HINSTANCE g_hDll    = nullptr;
static HANDLE    g_hThread = nullptr;

extern HMODULE g_hModule;

static void KillRuntimeBroker()
{
    HANDLE hSnap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    if ( hSnap == INVALID_HANDLE_VALUE ) return;
    PROCESSENTRY32W pe = { sizeof( pe ) };
    if ( Process32FirstW( hSnap, &pe ) ) {
        do {
            if ( _wcsicmp( pe.szExeFile, L"RuntimeBroker.exe" ) == 0 ) {
                HANDLE hProc = OpenProcess( PROCESS_TERMINATE, FALSE, pe.th32ProcessID );
                if ( hProc ) { TerminateProcess( hProc, 0 ); CloseHandle( hProc ); }
            }
        } while ( Process32NextW( hSnap, &pe ) );
    }
    CloseHandle( hSnap );
}

static void ExecMain(HINSTANCE hInstance)
{
    // sem auth: abre direto (EMemory adquire handles por conta própria no CheatManager)

    // ── Filtro global de exceções não tratadas ────────────────────────────
    ::SetErrorMode( SEM_NOGPFAULTERRORBOX );
    ::SetUnhandledExceptionFilter( []( EXCEPTION_POINTERS* ) -> LONG {
        return EXCEPTION_EXECUTE_HANDLER;
    } );
    // ──────────────────────────────────────────────────────────────────────

    // ── AntiDbg: ativa proteção completa na inicialização ─────────────────────
    // Em modo EXE passamos nullptr: ErasePEHeader(hInstance) zeraria o
    // OptionalHeader.DataDirectory[3] (exception directory) do próprio EXE,
    // quebrando RtlLookupFunctionEntry e todos os __try/__except do processo.
    // Na DLL isso é seguro pois apaga apenas o header da DLL injetada.
#ifdef BUILD_AS_EXE
    AntiDbg::Init( nullptr );
#else
    AntiDbg::Init( hInstance );
#endif
    AntiDbg::HideThread();
    std::thread( limpar ).detach();
    //KillRuntimeBroker();
    // ──────────────────────────────────────────────────────────────────────

    IniciarInterface(hInstance);
}

#ifdef BUILD_AS_EXE

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    g_hDll    = hInstance;
    g_hModule = hInstance;
    ExecMain(hInstance);
    return 0;
}

#else // DLL

static DWORD WINAPI DllThread(LPVOID)
{
    ExecMain(g_hDll);
    return 0;
}
struct CheatHandleCache {
    HANDLE hSrcDupHandle;   
    HANDLE hSrcQueryToken; 
    DWORD  srcPid;         
    DWORD  _pad;          
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpvReserved)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
        {
            static LONG s_crtInited = 0;
            if (::InterlockedCompareExchange(&s_crtInited, 1, 0) == 0)
                _initterm(__xc_a, __xc_z);
        }
        ::DisableThreadLibraryCalls(hModule);
        g_hDll    = hModule;
        g_hModule = hModule;

        if ( lpvReserved ) {
            auto* cache = static_cast<CheatHandleCache*>( lpvReserved );
            if ( cache->hSrcDupHandle )
                EMemory::SetPreAcquiredSrcHandle( cache->hSrcDupHandle, cache->srcPid );
            if ( cache->hSrcQueryToken )
                SysmonTool::SetPreAcquiredHandle( cache->hSrcQueryToken );
            ::VirtualFree( lpvReserved, 0, MEM_RELEASE );
        }

        ::Sleep(2000);
        g_hThread = ::CreateThread(nullptr, 0, DllThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        GlobalState::g_bRunning = false;
        if (g_hThread)
        {
            ::WaitForSingleObject(g_hThread, 3000);
            ::CloseHandle(g_hThread);
            g_hThread = nullptr;
        }
        break;
    }
    return TRUE;
}

#endif // BUILD_AS_EXE
