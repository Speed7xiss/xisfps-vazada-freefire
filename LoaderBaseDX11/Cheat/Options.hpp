#pragma once
#include <Windows.h>

namespace Cheat {
	class Options {
	public:
		struct VisualsStruct {
			struct ESPStruct {
				struct PlayersStruct {
					bool ESPLine = false;
					int  ESPLinePos = 0; // 0: Top, 1: Bottom

					bool ESPBox = false;
					int  ESPBoxStyle = 0; // 0: Normal, 1: Filled, 2: Corners

					bool ESPHealth = false;
					int  ESPHealthPos = 0; // 0: Left, 1: Right, 2: Top, 3: Bottom

					bool ESPDistance = false;
					int  ESPDistancePos = 3; // Default Bottom

					bool ESPSkeleton = false;

					bool ESPName = false;
					int  ESPNamePos   = 2; // 0: Left, 1: Right, 2: Top, 3: Bottom (default Top)
					int  ESPNameStyle = 0; // 0: Nick only, 1: Nick + HP, 2: BOT tag

					bool ESPWeapon = false;
					int  ESPWeaponPos = 3;   // 0: Left, 1: Right, 2: Top, 3: Bottom (default Bottom)
					int  ESPWeaponStyle = 0; // 0: Name only, 1: Icon + Name

					bool UseVisibilityColor = false;

					// TeamCheck — quando OFF (default), o ESP mostra TODOS os
					// players do lobby/partida (inclui teammates). Quando ON,
					// o filtro de teammate (UMAData.isTeammate OU TeamModeID
					// igual) esconde companheiros de squad — comportamento
					// tradicional de ESP em BR. Gate aplicado em SharedFrame.cpp
					// (filtro do snapshot) e AimModules.cpp (aim exclui teammate).
					bool TeamCheck = false;

					// Shadow pass (1px offset, semi-transparent black) por trás
					// de distance/name/weapon/GhostMode. Default true preserva
					// o visual atual; desligar reclama a metade do custo CPU do
					// text-draw (cada glyph vira 4v+6i no ImDrawList; a pass de
					// shadow gera o mesmo volume da fill pass).
					bool ESPTextShadow = true;

					float BoxColor[4]      = { 1.0f, 1.0f, 1.0f, 0.86f };
					float LineColor[4]     = { 1.0f, 1.0f, 1.0f, 0.60f };
					float SkeletonColor[4] = { 1.0f, 1.0f, 1.0f, 0.70f };
					float DistanceColor[4] = { 0.90f, 0.90f, 0.90f, 1.0f };
					float NameColor[4]     = { 1.0f, 1.0f, 1.0f, 1.0f };
					float WeaponColor[4]   = { 1.0f, 1.0f, 1.0f, 1.0f };

					// Cor aplicada a TODOS os elementos do ESP quando UseVisibilityColor
					// está ligado e o inimigo está atrás de parede (raycast bloqueado).
					// Antes: hardcoded IM_COL32(255,30,30,a) em Draw.cpp pickCol.
					// Agora: consumida por pickCol; alpha ainda copiado do color do
					// elemento (mantém a opacidade que o usuário escolheu por elemento).
					float HiddenColor[4]   = { 1.0f, 0.117f, 0.117f, 1.0f };

					float NameSize          = 15.0f;
					float DistanceSize      = 15.0f;
					float WeaponSize        = 15.0f;
					float BoxThickness      = 1.5f;
					// Antes hardcoded como 1.0f dentro do Draw.cpp — expostos para
					// o usuário controlar via sliders na aba Colors.
					float LineThickness     = 1.0f;
					float SkeletonThickness = 1.0f;

					// Distância máxima (m) até onde inimigos são desenhados no ESP.
					// Slider "Esp Render" na aba Visuals, range 1..1000.
					int EspRenderDistance = 120;
				} Players;
			} ESP;
		} Visuals;

		// Onde o ESP é renderizado. Trocar aqui força um Restart do
		// pipeline de render (Overlay + Interface + Cheat) para recriar
		// o device DX11 em cima do backend correto.
		enum EOverlayBackend {
			OverlayBackend_Normal  = 0, // janela topmost custom (default)
			OverlayBackend_Nvidia  = 1, // hijack da NVIDIA GeForce Overlay
			OverlayBackend_Discord = 2, // mirror via Discord Overlay (mapped file)
		};

		struct GeneralStruct {
			bool ShutDown = false;
			bool Restart  = false;
			int  MenuKey = VK_INSERT;
			bool StreamMode = true;
			// Cap do render loop (ms de sleep no fim de cada frame).
			// 0 = sem cap → loop roda a milhares de FPS, satura CPU e
			// contende a DXGI queue com a swap chain do BlueStacks (o
			// jogo perde frames mesmo com Present(0,0) sem VSync).
			//
			// Default: 2ms → cap teórico ~500 Hz. Como o ImGui NewFrame +
			// walk das entidades + RenderDrawData custa ~0.5-1ms, o FPS
			// prático do ESP fica em ~350-400. Alvo: 360 FPS — cobre
			// PCs high-end onde o jogo roda a 240+ com folga (ESP > FPS
			// do jogo é a única forma do overlay parecer 100% "colado"
			// no movimento). Em PCs mais fracos o custo real do loop
			// domina e naturalmente cai pra faixa correta.
			//
			// IMPORTANTE: o sleep no fim do frame usa Cheat::HiRes::
			// SleepMillis (waitable timer per-thread) — se fosse
			// std::this_thread::sleep_for, sem timeBeginPeriod global,
			// sleep_for(2) viraria 15ms (timer resolution default) e o
			// cap cairia pra ~60 FPS. Ver Cheat.cpp:PresentFrame call site.
			int  ThreadDelay = 2;
			int  OverlayBackend = OverlayBackend_Normal;
		} General;

		struct AimDMAStruct {
			// No Recoil
			bool NoRecoil = false;

			// Silent Aim
			bool  SilentAim = false;
			bool  SilentDrawFov = false;
			float SilentFov = 9999.0f;
			int   SilentAimBind = 0;
			bool  SilentVisibleCheck = false;
			// Modo de mira do Silent:
			//   0 = Head (mira sempre na cabeça)
			//   1 = Chest (mira sempre no torso)
			//   2 = Mix — alterna writes em blocos entre HIP e HEAD conforme
			//       SilentChestRate:SilentHeadRate. Ex: 5:2 = 5 blocos hip +
			//       2 blocos head, repete. Aumenta hit rate real (se head
			//       miss, hip pega) à custa de nem todos os hits serem
			//       headshot. Portado do silent de referência.
			int   SilentAimTarget = 0;
			int   SilentChestRate = 5; // usado só quando SilentAimTarget == 2
			int   SilentHeadRate  = 2;
			// Se true, gate os writes por EntityIsFiring do LP (só escreve
			// enquanto o player realmente está atirando). Reduz CPU idle e
			// footprint anti-cheat quando não tem bind (SilentAimBind=0).
			bool  SilentFireGate  = false;

			// SpinBot
			bool  SpinBot = false;
			int   SpinBotSpeed = 10;
			int   SpinBotBind = 0;

			// Aimbot Memory
			bool  AimBotMemory = false;
			bool  AimBotMemoryDrawFov = false;
			float AimBotMemoryFov = 200.0f;
			int   AimBotMemoryBind = 0;
			int   AimBotSmooth = 0;      // 0 = instantaneo (mais roubado), 1-100 = SLERP

			bool Precision = false;
			int  PrecisionBind = 0;
			bool BugarPixel = false;
			int  BugarPixelBind = 0;
			int  AtributarArmaSpeed = 0;
			bool Aimlock = false;
			int  AimlockBind = 0;

			bool GhostMode     = false;
			int  GhostModeBind = 0;

			bool AlokBuff         = false;
			// Dropdown de 3 itens (Alok 1/2/3), idx 0..2. Default = 2 (Alok 3,
			// tier máximo) — antes era 3, que apontava para "Alok 3" quando a
			// lista ainda tinha 4 opções (Natural + Alok 1/2/3).
			int  AlokBuffLevel    = 2;
			bool AlokBuffInfinite = false;

			bool FastMedkit = false;
			bool MoreDamage = false;
			bool FireDelay = false;
			bool SocoLonge = false;
			bool NoReload = false;

			// Infinite Ammo — mantém ReloadNoConsumeAmmoclip (PA+0x98) e
			// ShootNoReload (PA+0x99) sempre ativos, garantindo que o clipe
			// nunca zera e os pacotes de munição do inventário jamais são
			// consumidos no reabastecimento.
			bool InfiniteAmmo = false;

			// Speed Hack
			bool  SpeedHack = false;
			float SpeedScale = 2.0f;

			// Maxim StrongMedicine — acelera uso de itens de cura (EatSpeedScale 0x60)
			// EatSpeedScale = 0.5 => kit realizado em 2 segundos (limite minimo do jogo)
			bool MaximSkill = false;

			// Rapid Fire PA — m_FireIntervalScale (PA+0x18C)
			// Aimbot Scope — AimAssistOnSighting LerpTime exploit
			// Tranca o timer do aim assist nativo em t=0 enquanto scopado.
			// Forca do assist fica na curva maxima o tempo todo -> crosshair gruda no alvo.
			bool AimbotScope = false;

			// Aimbot Legit por offset (BoneSwap)
			bool LegitAimEnable        = false;
			bool LegitAimLeftShoulder  = false;
			bool LegitAimRightShoulder = false;
			int  LegitAimDelay         = 0;
			// Default = 0 (sem bind) — Enable Aim funciona só com a checkbox on/off,
			// sem exigir tecla segurada. Antes: VK_LBUTTON (mira só clicando com o
			// esquerdo). Backend em AimModules.cpp:999 trata key==0 como "sempre
			// ativo" (bool keyDown = (key == 0) || GetAsyncKeyState(key)&0x8000).
			int  LegitAimKey           = 0;
		} AimDMA;
	};
}

inline Cheat::Options g_Options;
