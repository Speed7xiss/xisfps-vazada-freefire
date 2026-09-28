#pragma once
#include <d3d11.h>
#include <functional>

namespace Overlay
{
	// Backend de renderização do ESP. A escolha define para onde o DX11
	// pinta: uma janela topmost custom (Normal) ou a janela da NVIDIA
	// GeForce Overlay (Nvidia).
	enum Mode {
		Mode_Normal  = 0,
		Mode_Nvidia  = 1,
		Mode_Discord = 2,
	};

	// Modo com que a próxima Initialize() vai subir o overlay. Deve ser
	// setado ANTES de Setup/Initialize; trocar em runtime exige Restart
	// do pipeline (feito automaticamente pelo EspMonitorLoop quando o
	// usuário muda o combo box da GUI).
	void SetMode(Mode m);
	Mode GetMode();

	void Setup(HWND TargetHWND);
	void Initialize();
	void ShutDown();

	void UpdateWindowPos();
	void SetupWindowProcHook(std::function<void(HWND, UINT, WPARAM, LPARAM)> Funtion);

	bool IsSettuped();
	bool IsInitialized();
	HWND GetOverlayWindow();
	HWND GetTargetWindow();

	void dxInitialize();
	void dxRefresh();

	void dxCreateRenderTarget();
	void dxCleanupRenderTarget();
	void dxCleanupDeviceD3D();

	ID3D11Device* dxGetDevice();
	ID3D11DeviceContext* dxGetDeviceContext();
	IDXGISwapChain* dxGetSwapChain();
	ID3D11RenderTargetView* dxGetRenderTarget();

	// Dimensões do RTV atual (equivalente ao GetClientRect da janela do
	// overlay — no Normal é a janela custom, no Nvidia é a do GeForce).
	int  GetRenderWidth();
	int  GetRenderHeight();

	// Finaliza o frame — em ambos os modos faz Present() no swap chain.
	void PresentFrame();

	// Reafirma HWND_TOPMOST no overlay a cada N frames — combate outros
	// apps que também setam topmost (Task Manager, outros overlays).
	// Chamado pelo render loop do Cheat.
	void EnsureTopmost();

	// Define o monitor preferido para o ESP overlay.
	// active=false → seguir a janela do jogo (comportamento padrão).
	// active=true  → fixar o canto superior-esquerdo em (x,y) — coordenadas
	//                de tela absolutas do monitor escolhido — com as mesmas
	//                dimensões da janela alvo (target window).
	// Chamado pela GUI quando o usuário muda o combo "Esp Monitor".
	void SetEspMonitorOrigin(long x, long y, bool active);
}
