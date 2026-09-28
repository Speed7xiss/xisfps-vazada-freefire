#pragma once
#include <atomic>

// ─── CheatStatus ─────────────────────────────────────────────────────────────
// Fase corrente do sistema de monitoramento do cheat (emu/jogo).
// Escrito pelo EspMonitorLoop (main.cpp) e por Cheat::Initialize (Cheat.cpp);
// lido pela GUI (gui.cpp) para renderizar a categoria "Cheat Status".
//
// Fluxo feliz:
//   WaitingEmulator  →  procurando HD-Player.exe
//   WaitingGame      →  emulador achado, aguardando o jogo (com.dts.freefir)
//   Starting         →  jogo achado, delay de 3s antes de subir o overlay
//   Running          →  overlay ativo e render loop em execução
		//
// 
// Fases de erro (sticky entre retentativas até uma tentativa bem-sucedida):
//   EmulatorIncompatible → emulador rodando, mas nem BstkREM.dll nem
//                          BstkSharedFolders.dll foram localizados no HD-Player.
//                          Indica versão de BlueStacks divergente dos offsets.
//   GameIncompatible     → processo com.dts.freefir está de pé no emulador,
//                          mas libil2cpp.so não aparece nos VMAs do jogo.
//                          Indica APK modificado / versão incompatível.
namespace CheatStatus {
	enum Phase : int {
		WaitingEmulator      = 0,
		WaitingGame          = 1,
		Starting             = 2,
		Running              = 3,
		EmulatorIncompatible = 4,
		GameIncompatible     = 5,
	};

	inline std::atomic<int> g_Phase{ WaitingEmulator };

	inline void  SetPhase( Phase p ) { g_Phase.store( static_cast<int>( p ) , std::memory_order_relaxed ); }
	inline Phase GetPhase( )         { return static_cast<Phase>( g_Phase.load( std::memory_order_relaxed ) ); }

	// True para fases que devem ser preservadas entre retentativas do
	// EspMonitorLoop — se Initialize sinalizou erro terminal, o loop
	// externo não deve sobrescrever para "Aguardando Jogo...".
	inline bool IsError( Phase p ) {
		return p == EmulatorIncompatible || p == GameIncompatible;
	}
}
