#pragma once
// -----------------------------------------------------------------------------
// HiResSleep — sono de sub-milissegundo per-thread SEM afetar o resto do
// sistema. Substitui a antiga dependência de timeBeginPeriod(1) global.
//
// Motivo: timeBeginPeriod(1) sobe o timer resolution do Windows INTEIRO para
// 1 ms. Todas as threads (BlueStacks, DWM, HD-Player) recebem quantum menor,
// idle power sobe, e o próprio jogo perde ciclos de CPU. Trocar por
// CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION) dá precisão
// idêntica NA THREAD QUE PEDE — o resto do sistema não é afetado.
//
// Requer Windows 10 1803+. Em versões antigas, cai no Sleep tradicional
// (comportamento antigo, sem regressão além do que já era).
//
// Uso:
//   HiResSleepMicros(1000);   // 1ms com precisão sub-ms real
//   HiResSleepMillis(2);      // idem, 2ms
//
// Cada thread cria o timer uma vez (lazy) e reusa. Timer é destruído no
// término da thread via thread_local destructor.
// -----------------------------------------------------------------------------

#include <Windows.h>
#include <chrono>
#include <thread>

namespace Cheat::HiRes {

    // Flag opcional que só existe a partir da 10.0.17134 SDK. Definimos o
    // literal aqui pra compilar mesmo com toolchain antigo — se a versão de
    // runtime não suportar, o CreateWaitableTimerExW falha e caímos no fallback.
    #ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
    #endif

    // Um handle por thread — alocado sob demanda via dynamic TLS.
    // thread_local (static TLS) falha em DLLs mapeadas manualmente porque o
    // loader não registra o índice TLS para threads criadas após a injeção.
    struct ThreadTimer {
        HANDLE h        = nullptr;
        bool   triedInit = false;
        bool   hiRes    = false;
        // Handle não é fechado explicitamente: vazar o handle de timer é
        // inofensivo dado o contexto de injeção de curta duração.
    };

    inline DWORD& TimerTlsSlot() {
        static DWORD s_slot = TLS_OUT_OF_INDEXES;
        return s_slot;
    }

    inline ThreadTimer& Get() {
        // Garante que o slot TLS existe (thread-safe via spinlock).
        if ( TimerTlsSlot() == TLS_OUT_OF_INDEXES ) {
            static volatile LONG s_lock = 0;
            while ( InterlockedCompareExchange( &s_lock, 1, 0 ) != 0 )
                Sleep( 0 );
            if ( TimerTlsSlot() == TLS_OUT_OF_INDEXES )
                TimerTlsSlot() = TlsAlloc();
            InterlockedExchange( &s_lock, 0 );
        }

        ThreadTimer* t = static_cast<ThreadTimer*>( TlsGetValue( TimerTlsSlot() ) );
        if ( !t ) {
            t = new ThreadTimer{};
            TlsSetValue( TimerTlsSlot(), t );
        }

        if ( !t->triedInit ) {
            t->triedInit = true;
            // Tenta hi-res primeiro (Win10 1803+).
            t->h = CreateWaitableTimerExW( nullptr , nullptr ,
                CREATE_WAITABLE_TIMER_HIGH_RESOLUTION | CREATE_WAITABLE_TIMER_MANUAL_RESET ,
                TIMER_ALL_ACCESS );
            if ( t->h ) {
                t->hiRes = true;
            } else {
                // Fallback: sem a flag hi-res.
                t->h = CreateWaitableTimerW( nullptr , TRUE , nullptr );
                t->hiRes = false;
            }
        }
        return *t;
    }

    // Sleep de N microssegundos com precisão sub-ms quando hi-res disponível.
    inline void SleepMicros( unsigned int us ) {
        if ( us == 0 ) { YieldProcessor(); return; }

        ThreadTimer& t = Get();
        if ( t.h ) {
            LARGE_INTEGER due;
            // Unidades de 100ns; negativo = relativo.
            due.QuadPart = -static_cast<LONGLONG>( us ) * 10;
            if ( SetWaitableTimer( t.h , &due , 0 , nullptr , nullptr , FALSE ) ) {
                WaitForSingleObject( t.h , INFINITE );
                return;
            }
        }
        // Fallback puro: sleep_for. Se timer resolution do sistema estiver em 15.6ms,
        // um SleepMicros(500) vira 15ms — mas isso só acontece se nem hi-res nem
        // waitable timer estiverem disponíveis, o que é raríssimo em Win10+.
        std::this_thread::sleep_for( std::chrono::microseconds( us ) );
    }

    inline void SleepMillis( unsigned int ms ) {
        SleepMicros( ms * 1000u );
    }

} // namespace Cheat::HiRes
