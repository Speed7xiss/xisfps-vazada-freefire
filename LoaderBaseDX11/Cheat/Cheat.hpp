#pragma once
#include <Windows.h>
#include <thread>
#include <cstdint>
#include <vector>
#include <utility>

namespace Cheat {
	void Initialize( );
	DWORD WINAPI Unload( );

	extern uintptr_t LibraryAddress;
	extern uintptr_t LibUnityAddress;

	// ?????????????????????????????????????????????????????????????????????
	// DRAW offsets � agora hardcoded em Cheat.cpp (g_DrawOff inicializado
	// no .cpp). Pra atualizar: rode o OffsetsClient (C#) e cole o snippet
	// do offsets.txt em cima de g_DrawOff no Cheat.cpp.
	// ?????????????????????????????????????????????????????????????????????
	struct DrawOffsets {
		uint32_t BASEGF , GAMEFACADE , STATICGF , CURRENTMATCH , MATCHGAME;
		uint32_t MATCHSTATUS , LOCALPLAYER , TRANSFORM , FOLLOWCAM , CAMERA;
		uint32_t POSTRENDER , VIEWMATRIX , DICTIONARY , ENTITYCOUNT;
		uint32_t DICTVALUES , DICTBASE , DICTSTRIDE , DICTENTOFF;
		uint32_t AVATARMGR , AVATAR , AVATARDATA , ISVISIBLE , AVMGR_VISIBLE , TRANSFORMTYPE , ISTEAM;
		uint32_t IPRIDATAPOOL , MDATAS , HEALTHREP , REPDATA;
		uint32_t HEADNODE , ROOTNODE , ITRANSFORM;
		uint32_t MATRIXLIST , MATRIXIDX;
		uint32_t ENTITYNAME , ISCLIENTBOT;
		uint32_t WEAPONREP;
		uint32_t PROFILEPTR , NICKNAME;

		// Spectator / Observer chain — vive no MESMO objeto Match onde
		// LOCALPLAYER (Match::m_LocalPlayer) fica. Se Match+LOCALOBSERVER
		// != 0, o jogador está em modo espectador; o alvo observado sai de
		// Observer+OBSERVER_TARGET. Sem isso o Producer usa LocalPlayer
		// stale/zerado após a morte, ViewMatrix/entidades param de bater
		// com o que a câmera do jogo mostra e o ESP some no espectador.
		// Offsets validados contra ZmInternal FF v31 (mesmos que já batiam
		// pra MATCHGAME/MATCHSTATUS/LOCALPLAYER).
		uint32_t LOCALOBSERVER;    // Match::m_LocalObserver
		uint32_t OBSERVER_TARGET;  // Observer::m_TargetPlayer
	};
	extern DrawOffsets g_DrawOff;
	// Registro runtime dos offsets do Draw (aba Offsets do website). Definido em Cheat.cpp.
	const std::vector<std::pair<const char*, uint32_t*>>& DrawOffTable();
}
