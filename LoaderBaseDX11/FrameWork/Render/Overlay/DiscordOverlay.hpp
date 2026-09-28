#pragma once
// Discord Overlay Hijack via NAMED MEMORY-MAPPED FILE.
// Discord cria um mapping compartilhado no processo do jogo com nome
// "DiscordOverlay_Framebuffer_Memory_<PID>" e a DiscordHook64.dll dele
// copia os pixels desse mapping pra composição sobre o game.
// Fazendo OpenFileMappingA + MapViewOfFile pegamos acesso direto à
// mesma memória compartilhada — escrevemos pixels lá e o Discord
// composita. Sem RPM/WPM/kernel driver, sem hijackar buffer interno da
// DLL (o que não funciona).
// Descoberto via https://github.com/SamuelTulach/OverlayCord

#include <Windows.h>
#include <cstdint>

namespace DiscordOverlay
{
    // Lifecycle
    bool Init();          // abre o mapped file, valida struct
    void Shutdown();
    bool IsReady();
    bool TryReinit();     // shutdown + Init (Discord reiniciou etc.)

    // Debug console — quando ligado, todos os passos do Init/EndFrame/etc
    // vão pro console alocado. Alocar antes de chamar Init pra ver a
    // detecção do processo, abertura do mapping e stats por frame.
    void EnableDebugConsole();
    void DisableDebugConsole();
    bool IsDebugConsoleEnabled();

    // Geometry (dims lidas do mapped file — podem mudar em resize/F11)
    int  GetWidth();
    int  GetHeight();

    // Ponteiro DIRETO pro pixel data no mapped file compartilhado. O
    // renderer preenche essa memória e o Discord composita imediatamente
    // no próximo BeginFrame++ (sem cópia intermediária).
    // Tamanho = GetWidth() * GetHeight() * 4 (BGRA).
    uint8_t* GetBackBuffer();
    int      GetBackBufferSize();

    // Frame sequencing — exatamente uma vez por render pass.
    void BeginFrame();    // verifica resize (não zera pixels — a cópia sobrescreve)
    void EndFrame();      // reafirma dims + bumpa FrameCount (sinal ao Discord)

    // Zera o mapped file + bumpa FrameCount. Chamado quando as dims do
    // swap chain divergem das dims do mapped file (típico: usuário
    // alterna fullscreen). Sem isso, o mapped file mantém os pixels da
    // resolução antiga renderizados até a próxima cópia bem-sucedida,
    // deixando um "ESP morto" visível no Discord por vários segundos.
    void ClearAndSignal();

    // Helper — cor float[4] (RGBA) -> BGRA uint32.
    inline uint32_t MakeBGRA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return ((uint32_t)b)
             | ((uint32_t)g << 8)
             | ((uint32_t)r << 16)
             | ((uint32_t)a << 24);
    }
    inline uint32_t FromFloat4(const float c[4]) {
        return MakeBGRA(
            (uint8_t)(c[0] * 255.0f),
            (uint8_t)(c[1] * 255.0f),
            (uint8_t)(c[2] * 255.0f),
            (uint8_t)(c[3] * 255.0f)
        );
    }
}
