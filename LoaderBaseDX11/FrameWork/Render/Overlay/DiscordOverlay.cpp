#include "DiscordOverlay.hpp"

#include <tlhelp32.h>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <algorithm>

// ────────────────────────────────────────────────────────────────────────────
// Descoberta chave (via https://github.com/SamuelTulach/OverlayCord):
//
// Discord expõe o framebuffer do overlay como um NAMED MEMORY-MAPPED FILE,
// nome padrão "DiscordOverlay_Framebuffer_Memory_<PID>". A DiscordHook64.dll
// injetada no processo do jogo copia esse mapped file para uma textura DX
// e composita sobre o game via draw calls no hook do Present.
//
// Erro que a versão anterior desse módulo cometia: escrever no CACHE INTERNO
// da DiscordHook.dll (o buffer que achamos por scan em fb=0x135FA8). Esse
// cache é sobrescrito a CADA frame pela cópia do mapped file, então nossos
// writes RPM/WPM/kernel-driver eram descartados. A abordagem correta é
// abrir o mapped file direto do nosso processo com OpenFileMappingA +
// MapViewOfFile e escrever no ponteiro compartilhado — Discord lê essa
// memória e composita.
// ────────────────────────────────────────────────────────────────────────────

namespace DiscordOverlay
{
    // Layout do header do mapped file — bate byte-por-byte com o struct
    // que a DiscordHook.dll usa (verificado pelo hex dump do cache interno).
    #pragma pack(push, 1)
    struct FBHeader {
        UINT Magic;       // +0   (Discord assina — não mexer)
        UINT FrameCount;  // +4   (incrementar por frame)
        UINT NoClue;      // +8   (não mexer)
        UINT Width;       // +12
        UINT Height;      // +16
        BYTE Buffer[1];   // +20  (BGRA pixels, tamanho variável)
    };
    #pragma pack(pop)

    constexpr int PIXEL_SIZE = 4;   // BGRA

    // Estado
    static HANDLE    g_mapHandle  = nullptr;
    static FBHeader* g_mapped     = nullptr;
    static DWORD     g_pid        = 0;
    static int       g_fbW        = 0;
    static int       g_fbH        = 0;
    static int       g_fbBytes    = 0;
    static bool      g_ready      = false;

    // Debug console
    static bool  g_ConsoleAllocated = false;
    static bool  g_DebugEnabled     = false;
    static FILE* g_ConsoleOutFile   = nullptr;
    static FILE* g_ConsoleErrFile   = nullptr;
    static FILE* g_ConsoleInFile    = nullptr;

    // Stats
    static int   g_StatFrames         = 0;
    static int   g_StatSentFrames     = 0;
    static DWORD g_StatsLastPrintTick = 0;

    // ──────────────────────────────────────────────────────────────────────
    // Debug logging
    // ──────────────────────────────────────────────────────────────────────
    static void DLog(const char* fmt, ...) {
        if (!g_DebugEnabled) return;
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        fflush(stdout);
    }

    void EnableDebugConsole() {
        if (g_ConsoleAllocated) { g_DebugEnabled = true; return; }
        AllocConsole();
        SetConsoleTitleA("Discord Overlay Debug");
        freopen_s(&g_ConsoleOutFile, "CONOUT$", "w", stdout);
        freopen_s(&g_ConsoleErrFile, "CONOUT$", "w", stderr);
        freopen_s(&g_ConsoleInFile,  "CONIN$",  "r", stdin);
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        g_ConsoleAllocated = true;
        g_DebugEnabled     = true;

        printf("========================================================\n");
        printf(" Discord Overlay Debug (via OpenFileMappingA)\n");
        printf("========================================================\n");
        fflush(stdout);
    }

    void DisableDebugConsole() {
        if (!g_ConsoleAllocated) return;
        g_DebugEnabled = false;
        if (g_ConsoleOutFile) { fclose(g_ConsoleOutFile); g_ConsoleOutFile = nullptr; }
        if (g_ConsoleErrFile) { fclose(g_ConsoleErrFile); g_ConsoleErrFile = nullptr; }
        if (g_ConsoleInFile)  { fclose(g_ConsoleInFile);  g_ConsoleInFile  = nullptr; }
        FreeConsole();
        g_ConsoleAllocated = false;
    }

    bool IsDebugConsoleEnabled() { return g_DebugEnabled; }

    // ──────────────────────────────────────────────────────────────────────
    // Process helpers
    // ──────────────────────────────────────────────────────────────────────
    static DWORD FindProcessId(const wchar_t* name)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        DWORD found = 0;
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, name) == 0) { found = pe.th32ProcessID; break; }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return found;
    }

    // Tenta abrir o mapped file de um PID específico. Retorna true se
    // o mapping existe (== Discord tem overlay ativo pra esse processo).
    static bool TryOpenMapping(DWORD pid, HANDLE& outHandle, FBHeader*& outMapped)
    {
        if (pid == 0) return false;

        // Nome canônico do mapping — descoberto no source do OverlayCord.
        char mappingName[128];
        _snprintf_s(mappingName, sizeof(mappingName), _TRUNCATE,
                    "DiscordOverlay_Framebuffer_Memory_%u", pid);

        DLog("[map] Tentando OpenFileMappingA(\"%s\") pra pid=%u\n", mappingName, pid);

        HANDLE h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, mappingName);
        if (!h) {
            DLog("[map]   OpenFileMappingA falhou (err=%lu) — Discord não criou esse mapping\n",
                 GetLastError());
            return false;
        }

        FBHeader* mapped = (FBHeader*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!mapped) {
            DLog("[map]   MapViewOfFile falhou (err=%lu)\n", GetLastError());
            CloseHandle(h);
            return false;
        }

        outHandle = h;
        outMapped = mapped;
        DLog("[map]   OK — mapped em %p, Magic=0x%X FrameCount=%u Width=%u Height=%u\n",
             (void*)mapped, mapped->Magic, mapped->FrameCount, mapped->Width, mapped->Height);
        return true;
    }

    // ──────────────────────────────────────────────────────────────────────
    // Init: procura um processo alvo com o mapping do Discord Overlay
    // Aberto e conecta. Retorna true só se achou e a struct parece válida.
    // ──────────────────────────────────────────────────────────────────────
    bool Init()
    {
        if (g_ready) {
            DLog("[init] já inicializado, ignorando.\n");
            return true;
        }

        DLog("[init] --- INICIANDO DISCORD OVERLAY VIA MAPPED FILE ---\n");

        // Discord cria o mapping no processo do jogo, não no do próprio Discord.
        // A ordem aqui prioriza o emulador (que é onde a DiscordHook64.dll
        // é injetada quando o BlueStacks é um Registered Game).
        const wchar_t* targetNames[] = {
            L"HD-Player.exe",
            L"BlueStacks.exe",
            L"Discord.exe",
            L"DiscordPTB.exe",
            L"DiscordCanary.exe",
        };

        for (auto* name : targetNames) {
            DWORD pid = FindProcessId(name);
            DLog("[find]   %ls -> pid=%u\n", name, pid);
            if (pid && TryOpenMapping(pid, g_mapHandle, g_mapped)) {
                g_pid = pid;
                break;
            }
        }

        if (!g_mapped) {
            DLog("[init] FALHOU — nenhum processo tem o mapping do Discord Overlay.\n");
            DLog("[init] Verificações:\n");
            DLog("[init]   - Discord está aberto?\n");
            DLog("[init]   - Discord > Settings > Game Overlay > Enable in-game overlay = ON?\n");
            DLog("[init]   - Discord > Registered Games > BlueStacks/HD-Player > ícone ligado?\n");
            DLog("[init]   - BlueStacks já foi aberto DEPOIS do overlay do Discord estar ligado?\n");
            return false;
        }

        // Valida dims. Discord seta valores razoáveis quando o overlay está
        // ativo pra um jogo. Se dims são 0 ou absurdos, o mapping existe mas
        // o overlay ainda não foi ativado.
        //
        // CRÍTICO: não usar dims de fallback (ex: 1920x1080) quando o mapping
        // real tem buffer de tamanho 0 ou menor. Isso causaria g_fbBytes >> tamanho
        // real do arquivo → overflow no Buffer → crash em qualquer memset/memcpy.
        // Solução: fechar o mapping e retornar false; PresentFrame tentará reinit.
        if (g_mapped->Width < 100 || g_mapped->Width > 3840 ||
            g_mapped->Height < 100 || g_mapped->Height > 2160)
        {
            DLog("[init] FALHOU: dims inválidas (%ux%u) — overlay não ativo ainda, tentando de novo.\n",
                 g_mapped->Width, g_mapped->Height);
            UnmapViewOfFile(g_mapped); g_mapped = nullptr;
            CloseHandle(g_mapHandle); g_mapHandle = nullptr;
            g_pid = 0;
            return false;
        }

        g_fbW = (int)g_mapped->Width;
        g_fbH = (int)g_mapped->Height;
        g_fbBytes = g_fbW * g_fbH * PIXEL_SIZE;

        g_ready = true;

        DLog("[init] OK — pid=%u mapping=%p dims=%dx%d (%d bytes)\n",
             g_pid, (void*)g_mapped, g_fbW, g_fbH, g_fbBytes);
        DLog("[init] --- BOOTSTRAP COMPLETO ---\n");

        g_StatFrames = g_StatSentFrames = 0;
        g_StatsLastPrintTick = GetTickCount();
        return true;
    }

    void Shutdown()
    {
        if (g_ready) DLog("[init] Shutdown — desmapeando + fechando handle\n");

        // CRÍTICO: zerar o mapped file ANTES de desmapear.
        //
        // O DiscordHook64.dll (injetado no processo do jogo pelo Discord) mantém
        // a própria view do mapping viva independente da nossa. Quando fechamos
        // sem limpar, os últimos pixels da ESP ficam no buffer compartilhado e
        // o hook continua compositando eles sobre o jogo até que o próprio
        // Discord decida invalidar (o que pode nunca acontecer nesta sessão).
        //
        // Solução: zera Buffer inteiro + bumpa FrameCount pra forçar o hook a
        // reler → ele composita uma frame vazia → ESP some da tela. Só então
        // desmapeamos com segurança.
        if (g_mapped && g_fbBytes > 0) {
            memset(g_mapped->Buffer, 0, g_fbBytes);
            g_mapped->FrameCount++;
        }

        if (g_mapped)     { UnmapViewOfFile(g_mapped); g_mapped = nullptr; }
        if (g_mapHandle)  { CloseHandle(g_mapHandle); g_mapHandle = nullptr; }
        g_pid = 0; g_fbW = 0; g_fbH = 0; g_fbBytes = 0;
        g_ready = false;
    }

    bool     IsReady()          { return g_ready; }
    int      GetWidth()         { return g_fbW; }
    int      GetHeight()        { return g_fbH; }

    // GetBackBuffer aponta DIRETO pra pixel data no mapped file — o
    // renderer preenche essa memória e o Discord composita direto.
    // Sem intermediário CPU->WPM->fb.
    uint8_t* GetBackBuffer()    { return g_mapped ? g_mapped->Buffer : nullptr; }
    int      GetBackBufferSize(){ return g_fbBytes; }

    bool TryReinit()
    {
        Shutdown();
        return Init();
    }

    // ──────────────────────────────────────────────────────────────────────
    // BeginFrame — só faz o polling de resize. NÃO zera o pixel data:
    //   * O backbuffer DX11 é limpo em dxRefresh (Clear (0,0,0,0))
    //   * O CopyResource + memcpy no PresentFrame SOBREESCREVE todos os
    //     pixels do mapped file com o backbuffer atual
    //   → o mapped file sempre contém uma frame completa após EndFrame.
    //
    // Antes tínhamos memset(g_mapped->Buffer, 0, ...) aqui — isso deixava
    // uma janela em que Discord podia ler pixels zerados no meio do write,
    // causando o flicker visto pelo usuário. Removido.
    // ──────────────────────────────────────────────────────────────────────
    void BeginFrame()
    {
        if (!g_ready || !g_mapped) return;

        // Poll de resize a cada frame (mapped file expõe dims reais do jogo).
        int nw = (int)g_mapped->Width;
        int nh = (int)g_mapped->Height;
        if (nw > 0 && nw <= 3840 && nh > 0 && nh <= 2160
            && (nw != g_fbW || nh != g_fbH))
        {
            // CRÍTICO: não atualizar g_fbBytes sem reabrir o mapping.
            // Quando o jogo muda de resolução, o Discord recria o named
            // section com novo tamanho. Atualizar g_fbBytes localmente
            // (para o valor maior) enquanto g_mapped ainda aponta pro
            // mapping antigo (menor) causa overflow no Buffer → crash.
            // TryReinit fecha o handle antigo e reabre o mapping correto.
            DLog("[fb] Resize detectado: %dx%d -> %dx%d — reiniciando mapping\n", g_fbW, g_fbH, nw, nh);
            TryReinit();
        }
    }

    void ClearAndSignal()
    {
        if (!g_ready || !g_mapped || g_fbBytes <= 0) return;
        memset(g_mapped->Buffer, 0, g_fbBytes);
        g_mapped->Width  = (UINT)g_fbW;
        g_mapped->Height = (UINT)g_fbH;
        g_mapped->FrameCount++;
    }

    // ──────────────────────────────────────────────────────────────────────
    // EndFrame — reafirma dims e incrementa FrameCount. O FrameCount++
    // é o sinal que a hook.dll usa pra saber que tem frame novo pra
    // compositar. Deve ser o ÚLTIMO write.
    // ──────────────────────────────────────────────────────────────────────
    void EndFrame()
    {
        if (!g_ready || !g_mapped) return;

        g_StatFrames++;

        // Reafirma dims (Discord pode ter alterado entre nossos writes).
        g_mapped->Width  = (UINT)g_fbW;
        g_mapped->Height = (UINT)g_fbH;

        // Frame count++ dispara o compositing. Deve ser o ÚLTIMO write —
        // se hook lê antes de nós terminarmos pixel data, vê frame parcial.
        g_mapped->FrameCount++;
        g_StatSentFrames++;

        // Stats a cada 2s
        if (g_DebugEnabled) {
            DWORD now = GetTickCount();
            if (now - g_StatsLastPrintTick >= 2000) {
                DLog("[stats] %d frames | %d sent | %dx%d | FrameCount=%u | Magic=0x%X\n",
                     g_StatFrames, g_StatSentFrames,
                     g_fbW, g_fbH, g_mapped->FrameCount, g_mapped->Magic);
                g_StatFrames = g_StatSentFrames = 0;
                g_StatsLastPrintTick = now;
            }
        }
    }
}
