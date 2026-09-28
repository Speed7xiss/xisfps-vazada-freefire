#include "Overlay.hpp"
#include "DiscordOverlay.hpp"
#include "../../Dependencies/ImGui/imgui.h"
#include <dwmapi.h>
#include <string>
#include <cstring>
#include <cstdint>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

struct WndRECT : public RECT {
	int Width() { return right - left; }
	int Height() { return bottom - top; }
};

static std::function<void(HWND, UINT, WPARAM, LPARAM)> pWindowProc;

static Overlay::Mode g_Mode = Overlay::Mode_Normal;

bool bSettuped = false;
bool bInitialized = false;
bool bDeviceInitialized = false;
bool bRenderTargetInitialized = false;

HWND hWindow = nullptr;
WNDCLASSEX WindowClass{};
HWND hTargetWindow = nullptr;
WndRECT wTargetWindowRect{};
static std::wstring g_ClassName{};

// Recursos DX11 comuns aos dois modos
ID3D11Device*           ID3dDevice           = nullptr;
ID3D11DeviceContext*    ID3dDeviceContext    = nullptr;
IDXGISwapChain*         ID3dSwapChain        = nullptr;
ID3D11RenderTargetView* ID3dRenderTargetView = nullptr;

// Nvidia-only: guardamos o ex-style original da janela do GeForce Overlay
// pra restaurar no ShutDown. Sem isso, WS_EX_TRANSPARENT permanece setado
// depois que o cheat sai — e o overlay do NVIDIA (Alt+Z) fica sem mouse
// input próprio.
static LONG_PTR g_NvidiaOriginalExStyle = 0;
static bool     g_NvidiaExStyleSaved    = false;

// Discord-only: PING-PONG staging texture pro readback do backbuffer DX11
// pro mapped file compartilhado do Discord Overlay. Duas stagings alternam:
// enquanto o CopyResource da frame N escreve em staging[N%2] (GPU async),
// o CPU faz Map/memcpy/Unmap em staging[(N-1)%2] (dados da frame anterior).
// Isso elimina o stall implícito do Map(READ) esperando a GPU terminar o
// CopyResource — antes o pipeline era serial, agora GPU e CPU trabalham
// em paralelo. Custo: 1 frame de latência a mais no que o Discord composita.
static ID3D11Texture2D* g_DiscordStaging[2] = { nullptr, nullptr };
static int              g_DiscordStagingW = 0;
static int              g_DiscordStagingH = 0;
static int              g_DiscordStageIdx = 0;   // qual índice recebe CopyResource neste frame
static bool             g_DiscordStagePrimed = false; // false até termos ao menos 1 frame na staging
static DWORD            g_DiscordLastReinitTick = 0;
// Throttle removido — o hash skip (ImDrawData FNV-1a) já descarta frames
// onde o ESP não mudou (>90% quando nenhum inimigo visível). Manter o
// gate de 16ms adicionava até ~16ms de atraso visual quando inimigos se
// moviam: o buffer do Discord ficava uma "janela" inteira atrás do render.
// Sem o gate o buffer é atualizado assim que o conteúdo muda, eliminando
// o lag. RAM write: ~0 em idle (hash skip); ~8MB/s @ 1080p quando há
// inimigos se movendo — dentro do budget normal.
static DWORD            g_DiscordLastCopyTick = 0; // mantido só pro fallback de reinit
// Skip-idêntico do readback: hash da ImDrawData da frame anterior aceita.
// Se a próxima frame produz um hash igual (ImGui vtx/idx idênticos + mesmas
// dims), o backbuffer será byte-idêntico ao já mapeado no Discord — pula
// CopyResource + Map(READ) + memcpy inteiros. Isso remove o fence GPU→CPU
// do Map em >90% dos frames idle (menu fechado, sem inimigos visíveis).
// Custo do hash: ~20-100 µs contra ~1-3 ms do readback evitado.
static uint64_t         g_LastDiscordDrawHash      = 0;
static bool             g_LastDiscordDrawHashValid = false;

// ─── GPU blit: downsample backbuffer (window res) → Discord mapped file res ──
// Quando a janela do overlay tem resolução diferente do que o Discord reporta
// no mapped file (resolução interna de render do jogo), o ESP seria renderizado
// em window res e o Discord esticaria com nearest-neighbor → pixelado. O blit
// GPU escala com LINEAR filtering antes do CopyResource→staging→memcpy,
// eliminando o pixelado. Sem custo CPU; custo GPU <0.1ms (uma CopyResource +
// um Draw com 3 vértices).
static ID3D11Texture2D*          g_BlitSrcTex       = nullptr; // window res, SRV
static ID3D11ShaderResourceView* g_BlitSrcSRV       = nullptr;
static ID3D11Texture2D*          g_BlitDstTex       = nullptr; // Discord res, RTV
static ID3D11RenderTargetView*   g_BlitDstRTV       = nullptr;
static ID3D11SamplerState*       g_BlitSampler      = nullptr;
static ID3D11VertexShader*       g_BlitVS           = nullptr;
static ID3D11PixelShader*        g_BlitPS           = nullptr;
static int                       g_BlitSrcW         = 0;
static int                       g_BlitSrcH         = 0;
static int                       g_BlitDstW         = 0;
static int                       g_BlitDstH         = 0;
static bool                      g_BlitShadersOk    = false;
static bool                      g_BlitShadersFailed= false;

// ────────────────────────────────────────────────────────────────────────────
// Helpers do Normal (janela topmost custom + swap chain)
// ────────────────────────────────────────────────────────────────────────────
static void CreateDeviceD3D_ForHwnd(HWND target) {
	DXGI_SWAP_CHAIN_DESC SwapChainDesc;
	ZeroMemory(&SwapChainDesc, sizeof(SwapChainDesc));
	SwapChainDesc.BufferDesc.Width = 0;
	SwapChainDesc.BufferDesc.Height = 0;
	SwapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	SwapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SwapChainDesc.SampleDesc.Count = 1;
	SwapChainDesc.SampleDesc.Quality = 0;
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.BufferCount = 2;
	SwapChainDesc.OutputWindow = target;
	SwapChainDesc.Windowed = TRUE;
	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	D3D_FEATURE_LEVEL FeatureLevel;
	const D3D_FEATURE_LEVEL FeatureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		FeatureLevelArray, 2, D3D11_SDK_VERSION, &SwapChainDesc, &ID3dSwapChain, &ID3dDevice,
		&FeatureLevel, &ID3dDeviceContext);
	if (FAILED(hr)) return;

	bDeviceInitialized = true;
}

// Variante BGRA do CreateDeviceD3D_ForHwnd — usada só pelo Mode_Discord.
// O mapped file do Discord Overlay é BGRA; usar o mesmo formato no swap
// chain evita swizzle byte-a-byte no readback (CopyResource é 1:1).
static void CreateDeviceD3D_ForHwnd_BGRA(HWND target) {
	DXGI_SWAP_CHAIN_DESC SwapChainDesc;
	ZeroMemory(&SwapChainDesc, sizeof(SwapChainDesc));
	SwapChainDesc.BufferDesc.Width = 0;
	SwapChainDesc.BufferDesc.Height = 0;
	SwapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	SwapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	SwapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	SwapChainDesc.SampleDesc.Count = 1;
	SwapChainDesc.SampleDesc.Quality = 0;
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.BufferCount = 2;
	SwapChainDesc.OutputWindow = target;
	SwapChainDesc.Windowed = TRUE;
	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	D3D_FEATURE_LEVEL FeatureLevel;
	const D3D_FEATURE_LEVEL FeatureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

	// BGRA_SUPPORT é obrigatório pra device criar swap chain em B8G8R8A8_UNORM
	UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags,
		FeatureLevelArray, 2, D3D11_SDK_VERSION, &SwapChainDesc, &ID3dSwapChain, &ID3dDevice,
		&FeatureLevel, &ID3dDeviceContext);
	if (FAILED(hr)) return;

	bDeviceInitialized = true;
}

// (Re)cria AMBAS as stagings ping-pong quando o backbuffer mudou de
// tamanho ou ainda não existem. Format sempre BGRA (mesmo do backbuffer
// no Mode_Discord). CPU_ACCESS_READ é o que permite Map+memcpy pra CPU.
static void EnsureDiscordStaging(int w, int h) {
	if (!ID3dDevice || w <= 0 || h <= 0) return;
	if (g_DiscordStaging[0] && g_DiscordStagingW == w && g_DiscordStagingH == h) return;

	for (int i = 0; i < 2; i++) {
		if (g_DiscordStaging[i]) { g_DiscordStaging[i]->Release(); g_DiscordStaging[i] = nullptr; }
	}
	g_DiscordStageIdx    = 0;
	g_DiscordStagePrimed = false;
	// Staging (re)criada — nenhuma cópia anterior é reaproveitável, então o
	// hash antigo não corresponde a nada mapeado. Força pelo menos um copy.
	g_LastDiscordDrawHashValid = false;

	D3D11_TEXTURE2D_DESC d{};
	d.Width = (UINT)w;
	d.Height = (UINT)h;
	d.MipLevels = 1;
	d.ArraySize = 1;
	d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_STAGING;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

	bool ok = true;
	for (int i = 0; i < 2; i++) {
		if (FAILED(ID3dDevice->CreateTexture2D(&d, nullptr, &g_DiscordStaging[i]))) { ok = false; break; }
	}
	if (ok) {
		g_DiscordStagingW = w;
		g_DiscordStagingH = h;
	} else {
		for (int i = 0; i < 2; i++) {
			if (g_DiscordStaging[i]) { g_DiscordStaging[i]->Release(); g_DiscordStaging[i] = nullptr; }
		}
		g_DiscordStagingW = 0;
		g_DiscordStagingH = 0;
	}
}

// Redimensiona o swap chain pra bater com (w,h). Usado pelo Mode_Discord
// pra manter o backbuffer sempre casado com as dims do mapped file — assim
// o ESP é renderizado na resolução exata que o Discord composita, sem
// scaling/borda preta/perda de qualidade quando o jogo troca de resolução
// (fullscreen ↔ janela, mudança de DPI, resize do BlueStacks, etc).
static void ResizeSwapChainTo(int w, int h) {
	if (!ID3dSwapChain || w <= 0 || h <= 0) return;

	DXGI_SWAP_CHAIN_DESC sd{};
	if (FAILED(ID3dSwapChain->GetDesc(&sd))) return;
	if ((int)sd.BufferDesc.Width == w && (int)sd.BufferDesc.Height == h) return;

	// RTV precisa ser destruído antes do ResizeBuffers (mantém ref no backbuffer).
	if (ID3dRenderTargetView) { ID3dRenderTargetView->Release(); ID3dRenderTargetView = nullptr; }
	bRenderTargetInitialized = false;

	if (SUCCEEDED(ID3dSwapChain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0))) {
		// Recria RTV
		ID3D11Texture2D* pBackBuffer = nullptr;
		if (SUCCEEDED(ID3dSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) && pBackBuffer) {
			ID3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &ID3dRenderTargetView);
			pBackBuffer->Release();
			bRenderTargetInitialized = true;
		}
	}
}

// ────────────────────────────────────────────────────────────────────────────
// GPU Blit helpers (Discord mode somente)
// ────────────────────────────────────────────────────────────────────────────

// Cria VS + PS do blit na primeira chamada usando bytecodes pré-compilados
// offline via fxc.exe /T vs_4_0 /O1 e /T ps_4_0 /O1.
//
// NÃO usa LoadLibraryA(d3dcompiler_47.dll) em runtime — essa DLL foi a causa
// do crash no Chrome: o Chrome detecta LoadLibrary de módulos não-autorizados
// e mata o processo ~6s depois. Com bytecodes pré-compilados não há LoadLibrary
// e não há custo de compilação HLSL (~300ms), resolvendo os dois problemas.
static void EnsureBlitShaders() {
    if (g_BlitShadersOk || g_BlitShadersFailed || !ID3dDevice) return;

    // Bytecodes gerados por:
    //   fxc.exe /T vs_4_0 /E main /O1 blit_vs.hlsl /Fo blit_vs.cso
    //   fxc.exe /T ps_4_0 /E main /O1 blit_ps.hlsl /Fo blit_ps.cso
    // Full-screen triangle via SV_VertexID (sem vertex buffer).
    // PS: Texture2D.Sample com SamplerState LINEAR CLAMP.
    static const BYTE vsCode[] = {
        0x44, 0x58, 0x42, 0x43, 0xE0, 0x23, 0x25, 0x73, 0x78, 0x4A, 0xFF, 0x82, 0xF4, 0x37, 0xA8, 0x65,
        0x70, 0x6C, 0x84, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x90, 0x02, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xB4, 0x00, 0x00, 0x00, 0x0C, 0x01, 0x00, 0x00,
        0x14, 0x02, 0x00, 0x00, 0x52, 0x44, 0x45, 0x46, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x04, 0xFE, 0xFF,
        0x00, 0x01, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x4D, 0x69, 0x63, 0x72, 0x6F, 0x73, 0x6F, 0x66,
        0x74, 0x20, 0x28, 0x52, 0x29, 0x20, 0x48, 0x4C, 0x53, 0x4C, 0x20, 0x53, 0x68, 0x61, 0x64, 0x65,
        0x72, 0x20, 0x43, 0x6F, 0x6D, 0x70, 0x69, 0x6C, 0x65, 0x72, 0x20, 0x31, 0x30, 0x2E, 0x31, 0x00,
        0x49, 0x53, 0x47, 0x4E, 0x2C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x53, 0x56, 0x5F, 0x56, 0x65, 0x72, 0x74, 0x65,
        0x78, 0x49, 0x44, 0x00, 0x4F, 0x53, 0x47, 0x4E, 0x50, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x03, 0x0C, 0x00, 0x00, 0x53, 0x56, 0x5F, 0x50, 0x4F, 0x53, 0x49, 0x54, 0x49, 0x4F, 0x4E, 0x00,
        0x54, 0x45, 0x58, 0x43, 0x4F, 0x4F, 0x52, 0x44, 0x00, 0xAB, 0xAB, 0xAB, 0x53, 0x48, 0x44, 0x52,
        0x00, 0x01, 0x00, 0x00, 0x40, 0x00, 0x01, 0x00, 0x40, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x04,
        0x12, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x67, 0x00, 0x00, 0x04,
        0xF2, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x65, 0x00, 0x00, 0x03,
        0x32, 0x20, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00,
        0x36, 0x00, 0x00, 0x08, 0xC2, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F,
        0x01, 0x00, 0x00, 0x0A, 0x32, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x10, 0x10, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x37, 0x00, 0x00, 0x0F, 0x32, 0x20, 0x10, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00,
        0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x40, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x80, 0xBF, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x37, 0x00, 0x00, 0x0F, 0x32, 0x20, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x46, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
        0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3E, 0x00, 0x00, 0x01, 0x53, 0x54, 0x41, 0x54, 0x74, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    }; // 656 bytes, vs_4_0

    static const BYTE psCode[] = {
        0x44, 0x58, 0x42, 0x43, 0xAC, 0x4F, 0x99, 0x5F, 0xE9, 0x19, 0x87, 0x29, 0xB1, 0x5C, 0x23, 0x1E,
        0xD9, 0xC3, 0x17, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x38, 0x02, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0xC4, 0x00, 0x00, 0x00, 0x1C, 0x01, 0x00, 0x00, 0x50, 0x01, 0x00, 0x00,
        0xBC, 0x01, 0x00, 0x00, 0x52, 0x44, 0x45, 0x46, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x04, 0xFF, 0xFF,
        0x00, 0x01, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x5E, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x73, 0x00, 0x74, 0x00, 0x4D, 0x69, 0x63, 0x72,
        0x6F, 0x73, 0x6F, 0x66, 0x74, 0x20, 0x28, 0x52, 0x29, 0x20, 0x48, 0x4C, 0x53, 0x4C, 0x20, 0x53,
        0x68, 0x61, 0x64, 0x65, 0x72, 0x20, 0x43, 0x6F, 0x6D, 0x70, 0x69, 0x6C, 0x65, 0x72, 0x20, 0x31,
        0x30, 0x2E, 0x31, 0x00, 0x49, 0x53, 0x47, 0x4E, 0x50, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x03, 0x03, 0x00, 0x00, 0x53, 0x56, 0x5F, 0x50, 0x4F, 0x53, 0x49, 0x54, 0x49, 0x4F, 0x4E, 0x00,
        0x54, 0x45, 0x58, 0x43, 0x4F, 0x4F, 0x52, 0x44, 0x00, 0xAB, 0xAB, 0xAB, 0x4F, 0x53, 0x47, 0x4E,
        0x2C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5F, 0x54, 0x41, 0x52, 0x47, 0x45, 0x54, 0x00, 0xAB, 0xAB,
        0x53, 0x48, 0x44, 0x52, 0x64, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
        0x5A, 0x00, 0x00, 0x03, 0x00, 0x60, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x18, 0x00, 0x04,
        0x00, 0x70, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x00, 0x00, 0x62, 0x10, 0x00, 0x03,
        0x32, 0x10, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x65, 0x00, 0x00, 0x03, 0xF2, 0x20, 0x10, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x45, 0x00, 0x00, 0x09, 0xF2, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x46, 0x10, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x46, 0x7E, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x60, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x01, 0x53, 0x54, 0x41, 0x54,
        0x74, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    }; // 568 bytes, ps_4_0

    bool ok = true;
    if (FAILED(ID3dDevice->CreateVertexShader(vsCode, sizeof(vsCode), nullptr, &g_BlitVS))) ok = false;
    if (ok && FAILED(ID3dDevice->CreatePixelShader(psCode, sizeof(psCode), nullptr, &g_BlitPS))) ok = false;

    if (!ok) { g_BlitShadersFailed = true; return; }
    g_BlitShadersOk = true;
}

// Blit GPU: src (bbW×bbH, backbuffer) → g_BlitDstTex (dstW×dstH) com LINEAR.
// Recria recursos apenas quando resolução muda. Retorna true se bem-sucedido.
static bool DoGPUBlit(ID3D11Texture2D* src, int srcW, int srcH, int dstW, int dstH) {
    if (!ID3dDevice || !ID3dDeviceContext) return false;

    EnsureBlitShaders();
    if (!g_BlitShadersOk) return false;

    // Sampler LINEAR CLAMP (uma vez).
    if (!g_BlitSampler) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter         = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD         = D3D11_FLOAT32_MAX;
        if (FAILED(ID3dDevice->CreateSamplerState(&sd, &g_BlitSampler))) return false;
    }

    // Textura fonte intermediária (window res, BIND_SHADER_RESOURCE).
    // Necessária porque o swap chain backbuffer não tem DXGI_USAGE_SHADER_INPUT
    // por padrão, então não podemos criar SRV diretamente dele. A CopyResource
    // GPU→GPU é rápida (<0.1ms) e torna a fonte segura para SRV.
    if (!g_BlitSrcTex || g_BlitSrcW != srcW || g_BlitSrcH != srcH) {
        if (g_BlitSrcSRV) { g_BlitSrcSRV->Release(); g_BlitSrcSRV = nullptr; }
        if (g_BlitSrcTex) { g_BlitSrcTex->Release(); g_BlitSrcTex = nullptr; }
        D3D11_TEXTURE2D_DESC d{};
        d.Width = (UINT)srcW; d.Height = (UINT)srcH;
        d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(ID3dDevice->CreateTexture2D(&d, nullptr, &g_BlitSrcTex))) return false;
        if (FAILED(ID3dDevice->CreateShaderResourceView(g_BlitSrcTex, nullptr, &g_BlitSrcSRV))) {
            g_BlitSrcTex->Release(); g_BlitSrcTex = nullptr; return false;
        }
        g_BlitSrcW = srcW; g_BlitSrcH = srcH;
    }

    // Textura destino (Discord res, BIND_RENDER_TARGET).
    if (!g_BlitDstTex || g_BlitDstW != dstW || g_BlitDstH != dstH) {
        if (g_BlitDstRTV) { g_BlitDstRTV->Release(); g_BlitDstRTV = nullptr; }
        if (g_BlitDstTex) { g_BlitDstTex->Release(); g_BlitDstTex = nullptr; }
        D3D11_TEXTURE2D_DESC d{};
        d.Width = (UINT)dstW; d.Height = (UINT)dstH;
        d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(ID3dDevice->CreateTexture2D(&d, nullptr, &g_BlitDstTex))) return false;
        if (FAILED(ID3dDevice->CreateRenderTargetView(g_BlitDstTex, nullptr, &g_BlitDstRTV))) {
            g_BlitDstTex->Release(); g_BlitDstTex = nullptr; return false;
        }
        g_BlitDstW = dstW; g_BlitDstH = dstH;
    }

    // 1) Copia backbuffer → g_BlitSrcTex (GPU-to-GPU, assíncrono).
    ID3dDeviceContext->CopyResource(g_BlitSrcTex, src);

    // 2) Full-screen triangle: sample g_BlitSrcTex → render g_BlitDstTex.
    D3D11_VIEWPORT vp{};
    vp.Width = (FLOAT)dstW; vp.Height = (FLOAT)dstH; vp.MaxDepth = 1.0f;
    ID3dDeviceContext->RSSetViewports(1, &vp);
    ID3dDeviceContext->OMSetRenderTargets(1, &g_BlitDstRTV, nullptr);
    float bf[4] = {};
    ID3dDeviceContext->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
    ID3dDeviceContext->OMSetDepthStencilState(nullptr, 0);
    ID3dDeviceContext->RSSetState(nullptr);
    ID3dDeviceContext->IASetInputLayout(nullptr);
    ID3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3dDeviceContext->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ID3dDeviceContext->VSSetShader(g_BlitVS, nullptr, 0);
    ID3dDeviceContext->PSSetShader(g_BlitPS, nullptr, 0);
    ID3dDeviceContext->PSSetShaderResources(0, 1, &g_BlitSrcSRV);
    ID3dDeviceContext->PSSetSamplers(0, 1, &g_BlitSampler);
    ID3dDeviceContext->Draw(3, 0);

    // 3) Desvincula SRV e RTV para não segurar refs no próximo frame.
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ID3dDeviceContext->PSSetShaderResources(0, 1, &nullSRV);
    ID3D11RenderTargetView* nullRTV = nullptr;
    ID3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

    return true;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (pWindowProc)
		pWindowProc(hWnd, uMsg, wParam, lParam);

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// ────────────────────────────────────────────────────────────────────────────
// Helpers do modo NVIDIA
// Aplica o mesmo truque do overlay-nvidia-overlay-hijack: seta bit 0x20 em
// GWL_EXSTYLE, estende o non-client area via DWM, layered attributes +
// SetWindowPos topmost. Resultado: a janela do overlay da NVIDIA vira uma
// superfície transparente topmost onde podemos desenhar via DX11.
// ────────────────────────────────────────────────────────────────────────────
static HWND FindNvidiaOverlayWindow() {
	// Ordem correta: (className, windowName) — foi assim que o repositório
	// original chamou. O CEF-OSC-WIDGET é o class atual (GeForce Experience).
	HWND h = FindWindowA("CEF-OSC-WIDGET", "NVIDIA GeForce Overlay");
	if (!h) h = FindWindowA("CEF-OSC-WIDGET", nullptr);
	return h;
}

// Termina o processo dono da janela CEF-OSC-WIDGET (overlay do NVIDIA
// GeForce Experience). Chamado quando saímos do modo Nvidia — garante
// estado 100% limpo (janela some, DXGI dela colapsa junto). O processo
// é auto-restartado pelo GeForce Experience quando o usuário abre o
// Alt+Z de novo.
static void KillNvidiaOverlayProcess() {
	HWND nv = FindWindowA("CEF-OSC-WIDGET", "NVIDIA GeForce Overlay");
	if (!nv) nv = FindWindowA("CEF-OSC-WIDGET", nullptr);
	if (!nv) return;

	DWORD pid = 0;
	GetWindowThreadProcessId(nv, &pid);
	if (!pid || pid == GetCurrentProcessId()) return;

	HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
	if (h) {
		TerminateProcess(h, 0);
		WaitForSingleObject(h, 500);  // até 500ms pra fechar
		CloseHandle(h);
	}
}

static bool PrepareNvidiaOverlay(HWND target) {
	if (!target) return false;

	// GWL_EXSTYLE. O overlay do NVIDIA GeForce já tem WS_EX_LAYERED (é uma
	// janela transparente do CEF), mas por padrão NÃO tem WS_EX_TRANSPARENT
	// (mouse pass-through). Se a gente esquece esse bit, a janela intercepta
	// TODOS os cliques do mouse — não dá pra clicar em nada abaixo. O
	// repositório original do overlay-hijack seta exatamente `info | 0x20`
	// que é WS_EX_TRANSPARENT — mesma flag aqui.
	SetLastError(0);
	LONG_PTR info = GetWindowLongPtrA(target, GWL_EXSTYLE);
	if (info == 0 && GetLastError() != 0) return false;

	// Salva o ex-style original UMA vez pra restaurar no ShutDown.
	// Chamadas subsequentes (ex.: re-init depois de erro) não sobrescrevem
	// o valor salvo — senão a segunda salvaria o valor já modificado.
	if (!g_NvidiaExStyleSaved) {
		g_NvidiaOriginalExStyle = info;
		g_NvidiaExStyleSaved    = true;
	}

	LONG_PTR desired = info | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
	SetLastError(0);
	LONG_PTR prev = SetWindowLongPtrA(target, GWL_EXSTYLE, desired);
	if (prev == 0 && GetLastError() != 0) return false;

	// Estende non-client area p/ toda a janela — necessário pro DWM
	// compor transparência no formato que o CEF-OSC-WIDGET usa.
	MARGINS margin;
	margin.cyBottomHeight = margin.cyTopHeight =
	margin.cxLeftWidth    = margin.cxRightWidth = -1;
	if (DwmExtendFrameIntoClientArea(target, &margin) != S_OK)
		return false;

	if (!SetLayeredWindowAttributes(target, 0x000000, 0xFF, LWA_ALPHA))
		return false;

	if (!SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0,
	                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
		return false;

	ShowWindow(target, SW_SHOWNOACTIVATE);
	return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Setup / SetMode / GetMode
// ────────────────────────────────────────────────────────────────────────────
namespace Overlay {
	void SetMode(Mode m) { g_Mode = m; }
	Mode GetMode()       { return g_Mode; }

	void Setup(HWND TargetHWND) {
		if (!TargetHWND) return;

		hTargetWindow = TargetHWND;
		if (GetClientRect(hTargetWindow, &wTargetWindowRect)) {
			MapWindowPoints(hTargetWindow, nullptr, reinterpret_cast<LPPOINT>(&wTargetWindowRect), 2);
			bSettuped = true;
		}
	}

	// ────────────────────────────────────────────────────────────────────
	// Initialize — despacha para uma das duas implementações abaixo.
	// ────────────────────────────────────────────────────────────────────
	static void InitializeNormal();
	static void InitializeNvidia();
	static void InitializeDiscord();

	void Overlay::Initialize() {
		if (!bSettuped) return;

		switch (g_Mode) {
			case Mode_Nvidia:  InitializeNvidia();  break;
			case Mode_Discord: InitializeDiscord(); break;
			case Mode_Normal:
			default:           InitializeNormal();  break;
		}
	}

	// ────────────────────────────────────────────────────────────────────
	// Normal — janela topmost custom + swap chain nela
	// ────────────────────────────────────────────────────────────────────
	static void InitializeNormal() {
		g_ClassName = L"Window_";
		UnregisterClass(g_ClassName.c_str(), GetModuleHandle(NULL));

		ZeroMemory(&WindowClass, sizeof(WNDCLASSEX));
		WindowClass.cbSize = sizeof(WNDCLASSEX);
		WindowClass.style = CS_HREDRAW | CS_VREDRAW;
		WindowClass.lpfnWndProc = WindowProc;
		WindowClass.cbClsExtra = 0;
		WindowClass.cbWndExtra = 0;
		WindowClass.hInstance = GetModuleHandle(NULL);
		WindowClass.hIcon = NULL;
		WindowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
		WindowClass.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
		WindowClass.lpszMenuName = NULL;
		WindowClass.lpszClassName = g_ClassName.c_str();
		WindowClass.hIconSm = NULL;

		ATOM classAtom = RegisterClassEx(&WindowClass);
		if (!classAtom) return;

		hWindow = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED,
			WindowClass.lpszClassName, L" ",
			WS_POPUP | WS_VISIBLE,
			wTargetWindowRect.left,
			wTargetWindowRect.top,
			wTargetWindowRect.Width(),
			wTargetWindowRect.Height(),
			nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

		if (!hWindow) return;

		SetLayeredWindowAttributes(hWindow, RGB(0, 0, 0), 255, LWA_ALPHA);

		MARGINS Margins = { wTargetWindowRect.left, wTargetWindowRect.top,
		                    wTargetWindowRect.Width(), wTargetWindowRect.Height() };
		DwmExtendFrameIntoClientArea(hWindow, &Margins);

		ShowWindow(hWindow, SW_SHOW);
		UpdateWindow(hWindow);

		CreateDeviceD3D_ForHwnd(hWindow);
		if (bDeviceInitialized) dxCreateRenderTarget();
		bInitialized = true;
	}

	// ────────────────────────────────────────────────────────────────────
	// Nvidia — hijack da janela do GeForce Overlay
	// SEM fallback: se o overlay do NVIDIA não estiver rodando, ESP
	// simplesmente não desenha. O comportamento anterior (cair no
	// Normal) desenhava numa janela topmost custom, o que confunde
	// o usuário — ele acha que está no Nvidia mas está no Normal.
	// Prefira falhar limpo — o usuário troca o combo pra Normal.
	// ────────────────────────────────────────────────────────────────────
	static void InitializeNvidia() {
		HWND nv = FindNvidiaOverlayWindow();
		if (!nv) return;

		if (!PrepareNvidiaOverlay(nv)) return;

		hWindow = nv;

		CreateDeviceD3D_ForHwnd(hWindow);
		if (!bDeviceInitialized) {
			hWindow = nullptr;
			return;
		}
		dxCreateRenderTarget();
		bInitialized = true;
	}

	// ────────────────────────────────────────────────────────────────────
	// Discord — mirror do render via memory-mapped file do Discord Overlay
	//
	// Sobe uma janela transparente topmost IDÊNTICA à do Mode_Normal (é ela
	// que o Interface usa pra menu + input do ImGui). Adicionalmente abre o
	// mapped file compartilhado do Discord Overlay. A cada PresentFrame o
	// backbuffer é copiado pra staging (CPU-readable) e daí memcpy pro
	// mapped file → Discord composita sobre o game em fullscreen (inclusive
	// quando a janela topmost do Normal fica ocluida).
	//
	// Format BGRA no swap chain evita swizzle no readback — o Discord já
	// espera BGRA.
	//
	// Falha suave: se DiscordOverlay::Init falha (Discord fechado / overlay
	// desligado / BlueStacks não é Registered Game), a janela transparente
	// sobe do mesmo jeito e o ESP funciona como Normal. A cada N segundos
	// tentamos TryReinit pra ligar o mirror se o usuário arrumar depois.
	// ────────────────────────────────────────────────────────────────────
	static void InitializeDiscord() {
		g_ClassName = L"Window_";
		UnregisterClass(g_ClassName.c_str(), GetModuleHandle(NULL));

		ZeroMemory(&WindowClass, sizeof(WNDCLASSEX));
		WindowClass.cbSize = sizeof(WNDCLASSEX);
		WindowClass.style = CS_HREDRAW | CS_VREDRAW;
		WindowClass.lpfnWndProc = WindowProc;
		WindowClass.cbClsExtra = 0;
		WindowClass.cbWndExtra = 0;
		WindowClass.hInstance = GetModuleHandle(NULL);
		WindowClass.hIcon = NULL;
		WindowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
		WindowClass.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
		WindowClass.lpszMenuName = NULL;
		WindowClass.lpszClassName = g_ClassName.c_str();
		WindowClass.hIconSm = NULL;

		ATOM classAtom = RegisterClassEx(&WindowClass);
		if (!classAtom) return;

		hWindow = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED,
			WindowClass.lpszClassName, L" ",
			WS_POPUP | WS_VISIBLE,
			wTargetWindowRect.left,
			wTargetWindowRect.top,
			wTargetWindowRect.Width(),
			wTargetWindowRect.Height(),
			nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

		if (!hWindow) return;

		// ALPHA=0 → janela topmost totalmente invisível LOCALMENTE.
		//
		// A ESP + o menu são renderizados normalmente no swap chain, mas o
		// backbuffer é enviado APENAS ao Discord via mapped file. O usuário
		// não vê a janela local — só o que o Discord composita sobre o jogo.
		// Isso evita a "dupla overlay" (topmost local + composite do Discord)
		// que causava duplicação visual + flicker.
		//
		// A janela ainda existe como HWND: recebe input (menu aberto captura
		// cliques mesmo invisível) e serve de output target para o DX11.
		SetLayeredWindowAttributes(hWindow, RGB(0, 0, 0), 0, LWA_ALPHA);

		MARGINS Margins = { wTargetWindowRect.left, wTargetWindowRect.top,
		                    wTargetWindowRect.Width(), wTargetWindowRect.Height() };
		DwmExtendFrameIntoClientArea(hWindow, &Margins);

		ShowWindow(hWindow, SW_SHOW);
		UpdateWindow(hWindow);

		// BGRA no swap chain — casa direto com o formato do mapped file.
		CreateDeviceD3D_ForHwnd_BGRA(hWindow);
		if (bDeviceInitialized) dxCreateRenderTarget();

		// Pré-compila shaders do blit GPU AGORA (durante init), antes do
		// render loop começar. D3DCompile pode levar ~300ms; chamar no
		// PresentFrame bloquearia o render thread no primeiro frame →
		// lag perceptível de ~300ms logo após iniciar o modo Discord.
		if (bDeviceInitialized) EnsureBlitShaders();

		// Mirror é opcional: se Discord não estiver pronto, o modo continua
		// "invisível" localmente (alpha=0) e só ativa a saída visual quando
		// TryReinit consegue abrir o mapped file (Discord/BlueStacks aberto
		// depois do cheat). TryReinit é chamado periodicamente no Present.
		DiscordOverlay::Init();
		g_DiscordLastReinitTick = GetTickCount();

		bInitialized = true;
	}

	// ────────────────────────────────────────────────────────────────────
	// ShutDown — desmonta tudo (comum aos dois modos)
	//
	// Modo Nvidia:
	//   1) Drena GPU (Flush + ClearState) — desamarra recursos DXGI da
	//      janela foreign do CEF-OSC-WIDGET.
	//   2) Solta swap chain / device.
	//   3) MATA o processo dono do overlay do NVIDIA (TerminateProcess).
	//      GeForce Experience recarrega o overlay sob demanda quando o
	//      Alt+Z é usado.
	// ────────────────────────────────────────────────────────────────────
	void Overlay::ShutDown() {
		// (1) Drena GPU: unbinds targets/samplers/etc, força fila a completar.
		//     Sem isso, releases podem deixar referências pendentes.
		if (ID3dDeviceContext) {
			ID3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
			ID3dDeviceContext->ClearState();
			ID3dDeviceContext->Flush();
		}

		// (2) Release RTV, device+context+swapchain.
		dxCleanupRenderTarget();
		// Discord: solta AMBAS as stagings ping-pong antes do device sumir
		// (referenciam o device — leak se soltas depois).
		for (int i = 0; i < 2; i++) {
			if (g_DiscordStaging[i]) { g_DiscordStaging[i]->Release(); g_DiscordStaging[i] = nullptr; }
		}
		g_DiscordStagingW = g_DiscordStagingH = 0;
		g_DiscordStageIdx = 0;
		g_DiscordStagePrimed = false;
		// GPU blit resources (Discord mode)
		if (g_BlitSrcSRV)  { g_BlitSrcSRV->Release();  g_BlitSrcSRV  = nullptr; }
		if (g_BlitSrcTex)  { g_BlitSrcTex->Release();  g_BlitSrcTex  = nullptr; }
		if (g_BlitDstRTV)  { g_BlitDstRTV->Release();  g_BlitDstRTV  = nullptr; }
		if (g_BlitDstTex)  { g_BlitDstTex->Release();  g_BlitDstTex  = nullptr; }
		if (g_BlitSampler) { g_BlitSampler->Release(); g_BlitSampler = nullptr; }
		if (g_BlitVS)      { g_BlitVS->Release();      g_BlitVS      = nullptr; }
		if (g_BlitPS)      { g_BlitPS->Release();      g_BlitPS      = nullptr; }
		g_BlitSrcW = g_BlitSrcH = 0;
		g_BlitDstW = g_BlitDstH = 0;
		g_BlitShadersOk    = false;
		g_BlitShadersFailed= false;
		if (g_Mode == Mode_Discord) DiscordOverlay::Shutdown();
		dxCleanupDeviceD3D();

		if (hWindow) {
			if (g_Mode == Mode_Nvidia) {
				// (3) MATA o processo dono do overlay do NVIDIA.
				// Não precisamos restaurar ex-style / DWM margins / z-order
				// da janela — matar o processo destrói a janela junto.
				// Zeramos nossos flags pra próxima Init começar limpo.
				KillNvidiaOverlayProcess();
				g_NvidiaExStyleSaved    = false;
				g_NvidiaOriginalExStyle = 0;
				// Dá tempo do OS reprocessar WM_DESTROY antes do próximo
				// Init tentar achar uma nova janela do NVIDIA overlay.
				Sleep(200);
			} else {
				// Esconde ANTES de destruir — evita flash residual visível
				// enquanto a transição para o próximo modo acontece.
				ShowWindow(hWindow, SW_HIDE);
				DestroyWindow(hWindow);

				// Drena mensagens pendentes (WM_DESTROY, WM_NCDESTROY, WM_PAINT,
				// etc.) SÍNCRONAMENTE — sem isso a nova Init pode criar a nova
				// janela antes do OS terminar de destruir a antiga, e por um
				// breve instante as duas coexistem (causa "2 overlays" visíveis
				// durante a troca Normal → Discord).
				MSG msg;
				while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
			}
		}
		hWindow = nullptr;

		if (!g_ClassName.empty() && WindowClass.hInstance) {
			UnregisterClassW(g_ClassName.c_str(), WindowClass.hInstance);
		}

		WindowClass = WNDCLASSEX{};
		g_ClassName.clear();

		bInitialized = false;
		bSettuped = false;
	}

	// ─── Preferência de monitor para o ESP overlay ──────────────────────────
	static long  g_esp_mon_pref_x      = 0;
	static long  g_esp_mon_pref_y      = 0;
	static bool  g_esp_mon_pref_active = false;

	void Overlay::SetEspMonitorOrigin(long x, long y, bool active)
	{
		g_esp_mon_pref_x      = x;
		g_esp_mon_pref_y      = y;
		g_esp_mon_pref_active = active;
		// Força reavaliação imediata zerando o cache de posição.
		wTargetWindowRect = WndRECT{};
	}

	void Overlay::UpdateWindowPos() {
		if (g_Mode != Mode_Normal || !hWindow) return;
		WndRECT TargetWindowRect;
		if (!GetClientRect(hTargetWindow, &TargetWindowRect)) return;
		MapWindowPoints(hTargetWindow, nullptr, reinterpret_cast<LPPOINT>(&TargetWindowRect), 2);

		// Monitor preferido para ESP: âncora no canto superior-esquerdo do
		// monitor escolhido pelo usuário, mantendo as mesmas dimensões do
		// target (o jogo cabe dentro do monitor selecionado).
		if (g_esp_mon_pref_active) {
			long w = TargetWindowRect.Width();
			long h = TargetWindowRect.Height();
			TargetWindowRect.left   = g_esp_mon_pref_x;
			TargetWindowRect.top    = g_esp_mon_pref_y;
			TargetWindowRect.right  = g_esp_mon_pref_x + w;
			TargetWindowRect.bottom = g_esp_mon_pref_y + h;
		}

		// FIX FLICKERING (raiz do problema): esta função é chamada uma vez
		// por frame no render loop do ESP (~360 Hz com ThreadDelay=2). Antes,
		// MoveWindow disparava toda vez incondicionalmente, disparando
		// WM_MOVE + WM_SIZE + WM_WINDOWPOSCHANGING/CHANGED 360x por segundo.
		// Numa janela com WDA_EXCLUDEFROMCAPTURE (aplicado por EspStreamApply
		// via GreProtectSpriteContent), cada evento desses força o DWM a
		// reavaliar/reconstruir a redirect surface -> piscar CONSTANTE que os
		// clientes reportaram. Fix: só chamar MoveWindow quando o rect da
		// janela alvo realmente mudou (arrastar/redimensionar o BlueStacks).
		// Sem mudança -> zero chamadas ao DWM -> zero flickering.
		if (TargetWindowRect.left   != wTargetWindowRect.left   ||
		    TargetWindowRect.top    != wTargetWindowRect.top    ||
		    TargetWindowRect.right  != wTargetWindowRect.right  ||
		    TargetWindowRect.bottom != wTargetWindowRect.bottom) {
			MoveWindow(hWindow, TargetWindowRect.left, TargetWindowRect.top,
			           TargetWindowRect.Width(), TargetWindowRect.Height(), FALSE);
			wTargetWindowRect = TargetWindowRect;
		}
	}

	void Overlay::SetupWindowProcHook(std::function<void(HWND, UINT, WPARAM, LPARAM)> Funtion) {
		pWindowProc = Funtion;
	}

	void Overlay::dxInitialize() {
		// Kept for API compat. Cada Initialize<Mode> já cria o device
		// da forma que precisa; ninguém deveria estar chamando isso
		// externamente. Left as no-op.
	}

	void Overlay::dxRefresh() {
		if (!ID3dDeviceContext || !ID3dRenderTargetView) return;
		ID3dDeviceContext->OMSetRenderTargets(1, &ID3dRenderTargetView, nullptr);
		static float TransparentColor[4] = { 0, 0, 0, 0 };
		ID3dDeviceContext->ClearRenderTargetView(ID3dRenderTargetView, TransparentColor);

		// Viewport ajustado ao tamanho atual do overlay.
		D3D11_VIEWPORT vp = {};
		vp.TopLeftX = 0;
		vp.TopLeftY = 0;
		vp.Width    = (FLOAT)GetRenderWidth();
		vp.Height   = (FLOAT)GetRenderHeight();
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		ID3dDeviceContext->RSSetViewports(1, &vp);
	}

	void Overlay::dxCreateRenderTarget() {
		if (!ID3dSwapChain || !ID3dDevice) return;

		ID3D11Texture2D* pBackBuffer = nullptr;
		if (FAILED(ID3dSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) || !pBackBuffer) return;
		ID3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &ID3dRenderTargetView);
		pBackBuffer->Release();
		bRenderTargetInitialized = true;
	}

	void Overlay::dxCleanupRenderTarget() {
		if (ID3dRenderTargetView) { ID3dRenderTargetView->Release(); ID3dRenderTargetView = nullptr; }
		bRenderTargetInitialized = false;
	}

	void Overlay::dxCleanupDeviceD3D() {
		if (ID3dSwapChain)     { ID3dSwapChain->Release();     ID3dSwapChain     = nullptr; }
		if (ID3dDeviceContext) { ID3dDeviceContext->Release(); ID3dDeviceContext = nullptr; }
		if (ID3dDevice)        { ID3dDevice->Release();        ID3dDevice        = nullptr; }
		bDeviceInitialized = false;
	}

	bool Overlay::IsSettuped()    { return bSettuped; }
	bool Overlay::IsInitialized() { return bInitialized; }
	HWND Overlay::GetOverlayWindow() { return hWindow; }
	HWND Overlay::GetTargetWindow()  { return hTargetWindow; }

	ID3D11Device*           Overlay::dxGetDevice()        { return ID3dDevice; }
	ID3D11DeviceContext*    Overlay::dxGetDeviceContext() { return ID3dDeviceContext; }
	IDXGISwapChain*         Overlay::dxGetSwapChain()     { return ID3dSwapChain; }
	ID3D11RenderTargetView* Overlay::dxGetRenderTarget()  { return ID3dRenderTargetView; }

	int Overlay::GetRenderWidth() {
		// SEMPRE usa o tamanho real da janela overlay (= janela BlueStacks).
		// Em Mode_Discord, o blit GPU escala para a resolução do mapped file;
		// o ESP renderiza em resolução de display completa e fica nítido.
		if (!hWindow) return 0;
		RECT rc; GetClientRect(hWindow, &rc);
		return rc.right - rc.left;
	}
	int Overlay::GetRenderHeight() {
		if (!hWindow) return 0;
		RECT rc; GetClientRect(hWindow, &rc);
		return rc.bottom - rc.top;
	}

	// Reafirma HWND_TOPMOST periodicamente. Outros apps (Task Manager,
	// OBS, notifs do sistema) podem virar topmost e ficar acima do ESP.
	// Fazer SetWindowPos por frame é caro — chamamos ~1x por segundo.
	//
	// FIX FLICKERING: SetWindowPos(HWND_TOPMOST) chamado incondicionalmente
	// toda vez numa janela com WDA_EXCLUDEFROMCAPTURE (GreProtectSpriteContent)
	// força o DWM a reavaliar sua redirect surface a cada chamada, gerando um
	// frame de transição visível na tela (~1s de intervalo). A correção é verificar
	// primeiro se a janela já é TOPMOST via GWL_EXSTYLE — se sim, não chama
	// SetWindowPos. O overlay quase nunca perde o topmost em uso normal,
	// então a chamada real passa a ser rara e o flickering desaparece.
	void Overlay::EnsureTopmost() {
		if (!hWindow) return;

		static DWORD s_lastTick = 0;
		DWORD now = GetTickCount();
		if (now - s_lastTick < 1000) return;
		s_lastTick = now;

		// Só chama SetWindowPos se a janela realmente perdeu o WS_EX_TOPMOST.
		// Chamada incondicional + WDA_EXCLUDEFROMCAPTURE = flickering a cada ~1s.
		LONG exStyle = GetWindowLong( hWindow, GWL_EXSTYLE );
		if ( exStyle & WS_EX_TOPMOST ) return;

		SetWindowPos(hWindow, HWND_TOPMOST, 0, 0, 0, 0,
		             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
	}

	// FNV-1a 64-bit sobre a ImDrawData da frame — cobre TUDO que o ImGui vai
	// desenhar (vertex/index buffers de cada cmd list) mais as dims do alvo.
	// Como dxRefresh limpa o backbuffer pra cor constante transparente e
	// todo o resto passa por ImGui, hash igual ao da frame anterior aceita
	// implica backbuffer byte-idêntico → podemos pular o readback.
	// ImDrawVert é POD (pos/uv/col, 20 bytes), safe hashear direto.
	static inline uint64_t HashDiscordDrawData(ImDrawData* dd, int w, int h) {
		uint64_t h64 = 1469598103934665603ULL; // FNV offset basis
		auto mix = [&](const void* p, size_t n) {
			const uint8_t* b = (const uint8_t*)p;
			for (size_t i = 0; i < n; i++) {
				h64 ^= (uint64_t)b[i];
				h64 *= 1099511628211ULL;
			}
		};
		int meta[4] = { w, h, dd->TotalVtxCount, dd->TotalIdxCount };
		mix(meta, sizeof(meta));
		for (int i = 0; i < dd->CmdListsCount; i++) {
			const ImDrawList* cl = dd->CmdLists[i];
			if (cl->VtxBuffer.Size > 0)
				mix(cl->VtxBuffer.Data, (size_t)cl->VtxBuffer.Size * sizeof(ImDrawVert));
			if (cl->IdxBuffer.Size > 0)
				mix(cl->IdxBuffer.Data, (size_t)cl->IdxBuffer.Size * sizeof(ImDrawIdx));
		}
		return h64;
	}

	// ────────────────────────────────────────────────────────────────────
	// PresentFrame — swap chain Present. Comum aos três modos.
	//
	// Discord: DEPOIS do Present, faz readback do backbuffer pra staging
	// (CPU-readable) e memcpy pro mapped file do Discord Overlay. Assim o
	// ESP + menu aparecem também no overlay do Discord (visível em jogos
	// fullscreen onde janela topmost normal é ocluida).
	// ────────────────────────────────────────────────────────────────────
	void Overlay::PresentFrame() {
		if (!ID3dSwapChain) return;

		// Discord mode: garante que a janela topmost permanece INVISÍVEL
		// localmente (o usuário só vê a saída via composite do Discord).
		// A Interface::HandleMenuKey remove WS_EX_LAYERED quando o menu
		// abre — sem LAYERED, o alpha=0 do SetLayeredWindowAttributes é
		// ignorado e a janela vira opaca preta. Aqui re-forçamos o estado
		// invisível a cada frame (custo ~µs, sem impacto perceptível).
		if (g_Mode == Mode_Discord && hWindow) {
			LONG_PTR ex = GetWindowLongPtr(hWindow, GWL_EXSTYLE);
			if (!(ex & WS_EX_LAYERED)) {
				SetWindowLongPtr(hWindow, GWL_EXSTYLE, ex | WS_EX_LAYERED);
				SetLayeredWindowAttributes(hWindow, RGB(0, 0, 0), 0, LWA_ALPHA);
				SetWindowPos(hWindow, HWND_TOPMOST, 0, 0, 0, 0,
				             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			}
		}

		// FIX FLICKER: vsync ON em Normal/Nvidia (comportamento do projeto
		// antigo). Present(0,0) em janela WS_EX_LAYERED + WS_EX_TOPMOST
		// centenas de vezes por segundo estressa o DWM: cada Present força
		// flush da redirect surface do overlay, produzindo o pisca reportado.
		// Present(1,0) alinha o overlay ao ciclo de composição do DWM (o
		// mesmo que rasteriza o BlueStacks) — sem flicker e sem penalizar
		// FPS do jogo. Modo Discord mantém 0 porque quem consome é o
		// mapped file, não o compositor.
		UINT syncInterval = (g_Mode == Mode_Discord) ? 0 : 1;
		ID3dSwapChain->Present(syncInterval, 0);

		if (g_Mode != Mode_Discord || !ID3dDeviceContext) return;

		// Tenta reabrir o mapped file periodicamente se ainda não pegou.
		// Cobre o caso do usuário abrir o Discord/BlueStacks DEPOIS do cheat.
		if (!DiscordOverlay::IsReady()) {
			DWORD now = GetTickCount();
			if (now - g_DiscordLastReinitTick >= 2000) {
				g_DiscordLastReinitTick = now;
				DiscordOverlay::TryReinit();
			}
			return;
		}

		// Poll de resize do mapped file (dims podem mudar quando o jogo
		// troca resolução).
		DiscordOverlay::BeginFrame();

		int discordW = DiscordOverlay::GetWidth();
		int discordH = DiscordOverlay::GetHeight();
		if (discordW <= 0 || discordH <= 0) return;

		// Backbuffer da janela overlay (resolução = window client area).
		ID3D11Texture2D* backBuffer = nullptr;
		if (FAILED(ID3dSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) return;

		D3D11_TEXTURE2D_DESC bbDesc{};
		backBuffer->GetDesc(&bbDesc);
		const int bbW = (int)bbDesc.Width;
		const int bbH = (int)bbDesc.Height;

		// Staging SEMPRE no tamanho do mapped file Discord (saída final).
		// O blit GPU cuida de escalar se bbW×bbH ≠ discordW×discordH.
		EnsureDiscordStaging(discordW, discordH);
		if (!g_DiscordStaging[0] || !g_DiscordStaging[1]) { backBuffer->Release(); return; }

		// ─── Skip-idêntico: hash do ImDrawData na resolução de render ─────
		// Usa dims do backbuffer (window) porque é onde o ImGui renderizou.
		uint64_t drawHash = 0;
		bool     drawHashComputed = false;
		if (ImDrawData* dd = ImGui::GetDrawData()) {
			drawHash         = HashDiscordDrawData(dd, bbW, bbH);
			drawHashComputed = true;
			if (g_LastDiscordDrawHashValid && g_DiscordStagePrimed && drawHash == g_LastDiscordDrawHash) {
				backBuffer->Release();
				return;
			}
		}

		// ═══════════════════════════════════════════════════════════════════
		// PING-PONG + GPU BLIT (quando necessário)
		//
		// Se bbW×bbH == discordW×discordH (resoluções batem):
		//   CopyResource direto backbuffer → staging[writeIdx]  (caminho atual)
		//
		// Se bbW×bbH != discordW×discordH (janela ≠ render interno do jogo):
		//   DoGPUBlit: backbuffer (window res) → g_BlitDstTex (Discord res)
		//              via full-screen triangle + LINEAR sampler.
		//   CopyResource g_BlitDstTex → staging[writeIdx]
		//
		// Em ambos os casos, staging está em discordW×discordH e o memcpy
		// pro mapped file é idêntico.
		// ═══════════════════════════════════════════════════════════════════

		const int writeIdx = g_DiscordStageIdx;
		const int readIdx  = writeIdx ^ 1;

		// 1) Kick da GPU: preenche staging[writeIdx] com frame atual.
		if (bbW == discordW && bbH == discordH) {
			// Caminho rápido: resolução idêntica → CopyResource direto.
			ID3dDeviceContext->CopyResource(g_DiscordStaging[writeIdx], backBuffer);
			backBuffer->Release();
		} else {
			// Resolução diferente: blit GPU (window→Discord res) com LINEAR.
			bool blitOk = DoGPUBlit(backBuffer, bbW, bbH, discordW, discordH);
			backBuffer->Release();
			if (blitOk) {
				ID3dDeviceContext->CopyResource(g_DiscordStaging[writeIdx], g_BlitDstTex);
			} else {
				// Blit falhou (shaders não disponíveis): descarta frame.
				g_DiscordStageIdx    = readIdx;
				g_DiscordStagePrimed = false;
				return;
			}
		}

		// 2) CPU lê staging do frame ANTERIOR (GPU já terminou a copy dele).
		if (g_DiscordStagePrimed) {
			D3D11_MAPPED_SUBRESOURCE map{};
			if (SUCCEEDED(ID3dDeviceContext->Map(g_DiscordStaging[readIdx], 0, D3D11_MAP_READ, 0, &map))) {
				uint8_t* dst = DiscordOverlay::GetBackBuffer();
				if (dst) {
					const int copyRowBytes = discordW * 4;
					const uint8_t* src     = (const uint8_t*)map.pData;
					if ((int)map.RowPitch == copyRowBytes) {
						memcpy(dst, src, (size_t)copyRowBytes * discordH);
					} else {
						for (int y = 0; y < discordH; y++)
							memcpy(dst + y * copyRowBytes, src + y * map.RowPitch, copyRowBytes);
					}
				}
				ID3dDeviceContext->Unmap(g_DiscordStaging[readIdx], 0);
				DiscordOverlay::EndFrame();
			}
		}

		// 3) Rotaciona ping-pong.
		g_DiscordStageIdx    = readIdx;
		g_DiscordStagePrimed = true;

		// 4) Memoriza hash pra skip-idêntico no próximo frame.
		if (drawHashComputed) {
			g_LastDiscordDrawHash      = drawHash;
			g_LastDiscordDrawHashValid = true;
		}
	}
}
