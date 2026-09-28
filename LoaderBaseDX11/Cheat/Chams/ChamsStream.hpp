#pragma once

// ─── Chams StreamMode ────────────────────────────────────────────────────────
// Sistema alternativo ao Chams Default — usa FBO no target pra desenhar as
// silhuetas fora do backbuffer (invisível a captura de tela / OBS / Discord)
// e faz streaming das pixels via memória compartilhada até uma overlay do
// nosso lado que compõe por cima do BlueStacks com LWA_COLORKEY.
//
// Usa o handle já adquirido por EMemory::GetHandle() (duplicado de
// winlogon/wininit/lsass) — nunca chama OpenProcess direto no emulador,
// portanto não gera Sysmon Event ID 10.
//
// StreamMode invisível via SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)
// (usermode, sem dependência de driver).

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dwmapi.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

#include "StreamContext.h"
#include "../EMemory.hpp"               // EMemory::GetHandle(), EMemory::GetProcessPid()
#include "../AimModules/HiResSleep.hpp" // Cheat::HiRes::SleepMillis

#pragma comment( lib , "psapi.lib" )
#pragma comment( lib , "dwmapi.lib" )
#pragma comment( lib , "winmm.lib" )

#ifndef WDA_EXCLUDEFROMCAPTURE
#  define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_NONE
#  define WDA_NONE 0x00000000
#endif

namespace Chams {
namespace Stream {

// ─── Shellcode HookedglDrawElements @ RVA 0x1214 (1902 bytes) ────────────────
static const unsigned char g_streamShellcode[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x89,
    0x48, 0x08, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x81,
    0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0xB8, 0x0D, 0xF0, 0xAD, 0xDE, 0xBE, 0xBA, 0xDE, 0xC0, 0x4D,
    0x8B, 0xE1, 0x48, 0x89, 0x45, 0xE8, 0x45, 0x8B, 0xE8, 0x48, 0x8B, 0x75, 0xE8, 0x44, 0x8B, 0xF2,
    0x8B, 0xF9, 0x41, 0xBF, 0x01, 0x00, 0x00, 0x00, 0xF0, 0x44, 0x01, 0xBE, 0x74, 0x02, 0x00, 0x00,
    0x33, 0xDB, 0x33, 0xC0, 0xF0, 0x0F, 0xB1, 0x9E, 0x80, 0x02, 0x00, 0x00, 0x41, 0x3B, 0xC7, 0x0F,
    0x85, 0xAA, 0x00, 0x00, 0x00, 0x8B, 0x86, 0x7C, 0x02, 0x00, 0x00, 0x41, 0x3B, 0xC7, 0x75, 0x5B,
    0x48, 0x8B, 0x86, 0xB0, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x0C, 0x48, 0x8D, 0x96, 0x84,
    0x02, 0x00, 0x00, 0x41, 0x8B, 0xCF, 0xFF, 0xD0, 0x48, 0x8B, 0x86, 0xD8, 0x00, 0x00, 0x00, 0x48,
    0x85, 0xC0, 0x74, 0x0C, 0x48, 0x8D, 0x96, 0x8C, 0x02, 0x00, 0x00, 0x41, 0x8B, 0xCF, 0xFF, 0xD0,
    0x48, 0x8B, 0x86, 0x90, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x0C, 0x48, 0x8D, 0x96, 0x88,
    0x02, 0x00, 0x00, 0x41, 0x8B, 0xCF, 0xFF, 0xD0, 0x48, 0x89, 0x9E, 0x84, 0x02, 0x00, 0x00, 0x89,
    0x9E, 0x8C, 0x02, 0x00, 0x00, 0x87, 0x9E, 0x7C, 0x02, 0x00, 0x00, 0xB8, 0x02, 0x00, 0x00, 0x00,
    0x87, 0x86, 0x80, 0x02, 0x00, 0x00, 0x8B, 0xCF, 0x48, 0x8B, 0x46, 0x40, 0x4D, 0x8B, 0xCC, 0x45,
    0x8B, 0xC5, 0x41, 0x8B, 0xD6, 0xFF, 0xD0, 0xF0, 0xFF, 0x8E, 0x74, 0x02, 0x00, 0x00, 0x4C, 0x8D,
    0x9C, 0x24, 0x80, 0x00, 0x00, 0x00, 0x49, 0x8B, 0x5B, 0x38, 0x49, 0x8B, 0x73, 0x40, 0x49, 0x8B,
    0x7B, 0x48, 0x49, 0x8B, 0xE3, 0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5D, 0xC3, 0x48,
    0x8B, 0x46, 0x38, 0xB9, 0x50, 0x00, 0x00, 0x00, 0xFF, 0xD0, 0x41, 0x84, 0xC7, 0x74, 0x1F, 0x8B,
    0x86, 0x70, 0x02, 0x00, 0x00, 0x41, 0x03, 0xC7, 0x25, 0x01, 0x00, 0x00, 0x80, 0x7D, 0x09, 0x41,
    0x2B, 0xC7, 0x83, 0xC8, 0xFE, 0x41, 0x03, 0xC7, 0x87, 0x86, 0x70, 0x02, 0x00, 0x00, 0x83, 0xFF,
    0x04, 0x75, 0x93, 0x41, 0x81, 0xFE, 0x70, 0x17, 0x00, 0x00, 0x7C, 0x8A, 0x48, 0x39, 0x5E, 0x48,
    0x75, 0x11, 0x48, 0x8B, 0x46, 0x30, 0x48, 0x8D, 0x8E, 0xF0, 0x00, 0x00, 0x00, 0xFF, 0xD0, 0x48,
    0x89, 0x46, 0x48, 0x48, 0x8B, 0x06, 0x48, 0x8D, 0x55, 0xD0, 0xB9, 0x8D, 0x8B, 0x00, 0x00, 0xFF,
    0xD0, 0x8B, 0x4D, 0xD0, 0x85, 0xC9, 0x0F, 0x84, 0x5A, 0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0x46, 0x48,
    0x48, 0x8D, 0x96, 0x10, 0x01, 0x00, 0x00, 0xFF, 0xD0, 0x83, 0xF8, 0xFF, 0x0F, 0x84, 0x44, 0xFF,
    0xFF, 0xFF, 0x33, 0xC0, 0xF0, 0x0F, 0xB1, 0x9E, 0x70, 0x02, 0x00, 0x00, 0x41, 0x3B, 0xC7, 0x0F,
    0x85, 0x31, 0xFF, 0xFF, 0xFF, 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F, 0x48,
    0x89, 0x5D, 0xE8, 0x48, 0x89, 0x45, 0xD8, 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0,
    0x3F, 0x48, 0x89, 0x45, 0xE0, 0x33, 0xC0, 0xF0, 0x0F, 0xB1, 0x9E, 0x78, 0x02, 0x00, 0x00, 0x0F,
    0x84, 0x38, 0x05, 0x00, 0x00, 0x8B, 0x86, 0x7C, 0x02, 0x00, 0x00, 0x85, 0xC0, 0x0F, 0x85, 0x17,
    0x03, 0x00, 0x00, 0x48, 0x8B, 0x46, 0x30, 0x48, 0x8D, 0x8E, 0x30, 0x01, 0x00, 0x00, 0xFF, 0xD0,
    0x48, 0x89, 0x86, 0x98, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0x50, 0x01, 0x00, 0x00, 0x48, 0x8B,
    0x46, 0x30, 0xFF, 0xD0, 0x48, 0x89, 0x86, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0x70, 0x01,
    0x00, 0x00, 0x48, 0x8B, 0x46, 0x30, 0xFF, 0xD0, 0x48, 0x89, 0x86, 0xA8, 0x00, 0x00, 0x00, 0x48,
    0x8D, 0x8E, 0x90, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x46, 0x30, 0xFF, 0xD0, 0x48, 0x89, 0x86, 0xB0,
    0x00, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0xB0, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x46, 0x30, 0xFF, 0xD0,
    0x48, 0x89, 0x86, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0xD0, 0x01, 0x00, 0x00, 0x48, 0x8B,
    0x46, 0x30, 0xFF, 0xD0, 0x48, 0x89, 0x86, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0xF0, 0x01,
    0x00, 0x00, 0x48, 0x8B, 0x46, 0x30, 0xFF, 0xD0, 0x48, 0x89, 0x86, 0xC8, 0x00, 0x00, 0x00, 0x48,
    0x8D, 0x8E, 0x10, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x46, 0x30, 0xFF, 0xD0, 0x48, 0x89, 0x86, 0xD0,
    0x00, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0x30, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x46, 0x30, 0xFF, 0xD0,
    0x48, 0x89, 0x86, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8E, 0x50, 0x02, 0x00, 0x00, 0x48, 0x8B,
    0x46, 0x30, 0xFF, 0xD0, 0x48, 0x89, 0x86, 0xE0, 0x00, 0x00, 0x00, 0x48, 0x39, 0x9E, 0x98, 0x00,
    0x00, 0x00, 0x0F, 0x84, 0x2E, 0x02, 0x00, 0x00, 0x48, 0x39, 0x9E, 0xA0, 0x00, 0x00, 0x00, 0x0F,
    0x84, 0x21, 0x02, 0x00, 0x00, 0x48, 0x39, 0x9E, 0xA8, 0x00, 0x00, 0x00, 0x0F, 0x84, 0x14, 0x02,
    0x00, 0x00, 0x48, 0x39, 0x9E, 0xB8, 0x00, 0x00, 0x00, 0x0F, 0x84, 0x07, 0x02, 0x00, 0x00, 0x48,
    0x39, 0x9E, 0xC0, 0x00, 0x00, 0x00, 0x0F, 0x84, 0xFA, 0x01, 0x00, 0x00, 0x48, 0x39, 0x9E, 0xC8,
    0x00, 0x00, 0x00, 0x0F, 0x84, 0xED, 0x01, 0x00, 0x00, 0x48, 0x39, 0x9E, 0xD0, 0x00, 0x00, 0x00,
    0x0F, 0x84, 0xE0, 0x01, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xD7, 0x01, 0x00, 0x00, 0x48,
    0x8B, 0x06, 0x48, 0x8D, 0x55, 0xF0, 0xB9, 0xA2, 0x0B, 0x00, 0x00, 0xFF, 0xD0, 0x44, 0x8B, 0x7D,
    0xF8, 0xB8, 0x80, 0x07, 0x00, 0x00, 0x8B, 0x7D, 0xFC, 0x44, 0x3B, 0xF8, 0x44, 0x0F, 0x4F, 0xF8,
    0xB8, 0x38, 0x04, 0x00, 0x00, 0x3B, 0xF8, 0x0F, 0x4F, 0xF8, 0x45, 0x85, 0xFF, 0x0F, 0x8E, 0xAE,
    0x01, 0x00, 0x00, 0x85, 0xFF, 0x0F, 0x8E, 0xA6, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x06, 0x48, 0x8D,
    0x55, 0xD0, 0xB9, 0xA6, 0x8C, 0x00, 0x00, 0x44, 0x89, 0xBE, 0x90, 0x02, 0x00, 0x00, 0x89, 0xBE,
    0x94, 0x02, 0x00, 0x00, 0x89, 0x5D, 0xD0, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x50, 0x48, 0x8D, 0x96,
    0x88, 0x02, 0x00, 0x00, 0xB9, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x58, 0xB9,
    0xE1, 0x0D, 0x00, 0x00, 0x8B, 0x96, 0x88, 0x02, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x60,
    0xB9, 0x08, 0x19, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x40, 0x44, 0x8B, 0xC1, 0xC7, 0x44, 0x24,
    0x38, 0x01, 0x14, 0x00, 0x00, 0x45, 0x8B, 0xCF, 0x89, 0x4C, 0x24, 0x30, 0x33, 0xD2, 0x89, 0x5C,
    0x24, 0x28, 0xB9, 0xE1, 0x0D, 0x00, 0x00, 0x89, 0x7C, 0x24, 0x20, 0xFF, 0xD0, 0x48, 0x8B, 0x46,
    0x68, 0xBA, 0x01, 0x28, 0x00, 0x00, 0xB9, 0xE1, 0x0D, 0x00, 0x00, 0x41, 0xB8, 0x00, 0x26, 0x00,
    0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x68, 0xBA, 0x00, 0x28, 0x00, 0x00, 0xB9, 0xE1, 0x0D, 0x00,
    0x00, 0x41, 0xB8, 0x00, 0x26, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x58, 0x33, 0xD2, 0xB9,
    0xE1, 0x0D, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x86, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x96,
    0x8C, 0x02, 0x00, 0x00, 0xB9, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x86, 0xC0, 0x00,
    0x00, 0x00, 0xB9, 0x41, 0x8D, 0x00, 0x00, 0x8B, 0x96, 0x8C, 0x02, 0x00, 0x00, 0xFF, 0xD0, 0x48,
    0x8B, 0x86, 0xC8, 0x00, 0x00, 0x00, 0x44, 0x8B, 0xCF, 0xBF, 0x41, 0x8D, 0x00, 0x00, 0x45, 0x8B,
    0xC7, 0x8B, 0xCF, 0xBA, 0xA6, 0x81, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x86, 0xC0, 0x00, 0x00,
    0x00, 0x33, 0xD2, 0x8B, 0xCF, 0xFF, 0xD0, 0x48, 0x8B, 0x86, 0x98, 0x00, 0x00, 0x00, 0x48, 0x8D,
    0x96, 0x84, 0x02, 0x00, 0x00, 0x41, 0xBF, 0x01, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xCF, 0xFF, 0xD0,
    0x48, 0x8B, 0x86, 0xA0, 0x00, 0x00, 0x00, 0x8D, 0x4F, 0xFF, 0x8B, 0x96, 0x84, 0x02, 0x00, 0x00,
    0xFF, 0xD0, 0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00, 0x8D, 0x57, 0x9F, 0x44, 0x8B, 0x8E, 0x88,
    0x02, 0x00, 0x00, 0x8D, 0x4F, 0xFF, 0x41, 0xB8, 0xE1, 0x0D, 0x00, 0x00, 0x89, 0x5C, 0x24, 0x20,
    0xFF, 0xD0, 0x48, 0x8B, 0x86, 0xD0, 0x00, 0x00, 0x00, 0x8D, 0x57, 0xBF, 0x44, 0x8B, 0x8E, 0x8C,
    0x02, 0x00, 0x00, 0x44, 0x8B, 0xC7, 0x8D, 0x7A, 0x40, 0x8B, 0xCF, 0xFF, 0xD0, 0x48, 0x8B, 0x86,
    0x80, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xDB, 0x0F, 0x57, 0xD2, 0x0F, 0x57, 0xC9, 0x0F, 0x57, 0xC0,
    0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x78, 0xB9, 0x00, 0x40, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x86,
    0xA0, 0x00, 0x00, 0x00, 0x8B, 0xCF, 0x8B, 0x55, 0xD0, 0xFF, 0xD0, 0x41, 0x8B, 0xC7, 0x87, 0x86,
    0x7C, 0x02, 0x00, 0x00, 0xEB, 0x11, 0x83, 0xC8, 0xFF, 0x87, 0x86, 0x7C, 0x02, 0x00, 0x00, 0xEB,
    0x09, 0x41, 0xBF, 0x01, 0x00, 0x00, 0x00, 0x8B, 0x7D, 0x30, 0x8B, 0x86, 0x7C, 0x02, 0x00, 0x00,
    0x41, 0x3B, 0xC7, 0x0F, 0x85, 0xCD, 0xFB, 0xFF, 0xFF, 0x48, 0x39, 0x9E, 0xA0, 0x02, 0x00, 0x00,
    0x0F, 0x84, 0xC0, 0xFB, 0xFF, 0xFF, 0x48, 0x8B, 0x06, 0x48, 0x8D, 0x55, 0xD0, 0xB9, 0xA6, 0x8C,
    0x00, 0x00, 0x89, 0x5D, 0xD0, 0xFF, 0xD0, 0x48, 0x8B, 0x06, 0x48, 0x8D, 0x55, 0xF0, 0xB9, 0xA2,
    0x0B, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x86, 0xA0, 0x00, 0x00, 0x00, 0xB9, 0xA8, 0x8C, 0x00,
    0x00, 0x8B, 0x55, 0xD0, 0xFF, 0xD0, 0x48, 0x8B, 0x86, 0xA0, 0x00, 0x00, 0x00, 0xB9, 0xA9, 0x8C,
    0x00, 0x00, 0x8B, 0x96, 0x84, 0x02, 0x00, 0x00, 0xFF, 0xD0, 0x8B, 0x86, 0x94, 0x02, 0x00, 0x00,
    0x33, 0xD2, 0x4C, 0x8B, 0x96, 0xE0, 0x00, 0x00, 0x00, 0x33, 0xC9, 0x44, 0x8B, 0x4D, 0xFC, 0x44,
    0x8B, 0x45, 0xF8, 0xC7, 0x44, 0x24, 0x48, 0x00, 0x26, 0x00, 0x00, 0xC7, 0x44, 0x24, 0x40, 0x00,
    0x01, 0x00, 0x00, 0x89, 0x44, 0x24, 0x38, 0x8B, 0x86, 0x90, 0x02, 0x00, 0x00, 0x89, 0x44, 0x24,
    0x30, 0x89, 0x5C, 0x24, 0x28, 0x89, 0x5C, 0x24, 0x20, 0x41, 0xFF, 0xD2, 0x48, 0x8B, 0x86, 0xA0,
    0x00, 0x00, 0x00, 0xB9, 0x40, 0x8D, 0x00, 0x00, 0x8B, 0x96, 0x84, 0x02, 0x00, 0x00, 0xFF, 0xD0,
    0x48, 0x8B, 0x86, 0x88, 0x00, 0x00, 0x00, 0x33, 0xD2, 0x44, 0x8B, 0x8E, 0x94, 0x02, 0x00, 0x00,
    0x33, 0xC9, 0x44, 0x8B, 0x86, 0x90, 0x02, 0x00, 0x00, 0xFF, 0xD0, 0xFF, 0x96, 0xE8, 0x00, 0x00,
    0x00, 0x8B, 0xC8, 0x44, 0x8B, 0xF8, 0x2B, 0x8E, 0x98, 0x02, 0x00, 0x00, 0x83, 0xF9, 0x0C, 0x0F,
    0x86, 0x94, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xBE, 0xA0, 0x02, 0x00, 0x00, 0x33, 0xC0, 0xF0, 0x0F,
    0xB1, 0x1F, 0x48, 0x8B, 0x96, 0xA0, 0x02, 0x00, 0x00, 0xBB, 0x01, 0x00, 0x00, 0x00, 0x44, 0x8B,
    0x8E, 0x94, 0x02, 0x00, 0x00, 0x2B, 0xD8, 0x48, 0x8B, 0x46, 0x70, 0x48, 0x83, 0xC2, 0x10, 0x44,
    0x8B, 0x86, 0x90, 0x02, 0x00, 0x00, 0x69, 0xCB, 0x00, 0x90, 0x7E, 0x00, 0x48, 0x03, 0xD1, 0x33,
    0xC9, 0x48, 0x89, 0x54, 0x24, 0x30, 0x33, 0xD2, 0xC7, 0x44, 0x24, 0x28, 0x01, 0x14, 0x00, 0x00,
    0xC7, 0x44, 0x24, 0x20, 0x08, 0x19, 0x00, 0x00, 0xFF, 0xD0, 0x8B, 0x86, 0x90, 0x02, 0x00, 0x00,
    0x89, 0x47, 0x04, 0x8B, 0x86, 0x94, 0x02, 0x00, 0x00, 0x89, 0x47, 0x08, 0xF0, 0xFF, 0x47, 0x0C,
    0x87, 0x1F, 0x48, 0x8B, 0x86, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xDB, 0x0F, 0x57, 0xD2, 0x0F,
    0x57, 0xC9, 0x0F, 0x57, 0xC0, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x78, 0xB9, 0x00, 0x40, 0x00, 0x00,
    0xFF, 0xD0, 0x44, 0x89, 0xBE, 0x98, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x46, 0x28, 0xF2, 0x0F, 0x10,
    0x4D, 0xE0, 0xF2, 0x0F, 0x10, 0x45, 0xD8, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x18, 0x41, 0xB0, 0x01,
    0x41, 0x8A, 0xD0, 0x41, 0x8A, 0xC8, 0x45, 0x33, 0xC9, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x10, 0xB9,
    0xE2, 0x0B, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x40, 0xBB, 0x04, 0x00, 0x00, 0x00, 0x8B,
    0xCB, 0x4D, 0x8B, 0xCC, 0x45, 0x8B, 0xC5, 0x41, 0x8B, 0xD6, 0xFF, 0xD0, 0x48, 0x8B, 0x86, 0xA0,
    0x00, 0x00, 0x00, 0xB9, 0x40, 0x8D, 0x00, 0x00, 0x8B, 0x55, 0xD0, 0xFF, 0xD0, 0x48, 0x8B, 0x86,
    0x88, 0x00, 0x00, 0x00, 0x44, 0x8B, 0x4D, 0xFC, 0x44, 0x8B, 0x45, 0xF8, 0x8B, 0x55, 0xF4, 0x8B,
    0x4D, 0xF0, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x28, 0xF2, 0x0F, 0x10, 0x4D, 0xD8, 0xF2, 0x0F, 0x10,
    0x45, 0xE8, 0xFF, 0xD0, 0x41, 0xB1, 0x01, 0x45, 0x8A, 0xC1, 0x41, 0x8A, 0xD1, 0x41, 0x8A, 0xC9,
    0x48, 0x8B, 0x46, 0x18, 0xFF, 0xD0, 0x8B, 0xCB, 0xE9, 0xCB, 0xF9, 0xFF, 0xFF, 0x48, 0x8B, 0x46,
    0x28, 0xF2, 0x0F, 0x10, 0x4D, 0xE0, 0xF2, 0x0F, 0x10, 0x45, 0xD8, 0xFF, 0xD0, 0x48, 0x8B, 0x46,
    0x18, 0x45, 0x33, 0xC9, 0x45, 0x8A, 0xC7, 0x41, 0x8A, 0xD7, 0x41, 0x8A, 0xCF, 0xFF, 0xD0, 0x48,
    0x8B, 0x46, 0x10, 0xB9, 0xE2, 0x0B, 0x00, 0x00, 0xFF, 0xD0, 0x48, 0x8B, 0x46, 0x40, 0xBB, 0x04,
    0x00, 0x00, 0x00, 0x8B, 0xCB, 0x4D, 0x8B, 0xCC, 0x45, 0x8B, 0xC5, 0x41, 0x8B, 0xD6, 0xFF, 0xD0,
    0x48, 0x8B, 0x46, 0x28, 0xF2, 0x0F, 0x10, 0x4D, 0xD8, 0xF2, 0x0F, 0x10, 0x45, 0xE8, 0xFF, 0xD0,
    0x45, 0x8A, 0xCF, 0x45, 0x8A, 0xC7, 0x41, 0x8A, 0xD7, 0x41, 0x8A, 0xCF, 0xEB, 0x92,
};

static const char* STREAM_TARGET_MODULE = "libOpenglRender.dll";
static const char* STREAM_EAT_SUB1     = "GLDispatch";
static const char* STREAM_EAT_SUB2     = "DrawElements";

// ─── Helpers (autocontidos neste namespace) ──────────────────────────────────

inline HMODULE FindRemoteModule_Stream( HANDLE hProc , const char* name ) {
    HMODULE mods[ 1024 ] = {};
    DWORD needed = 0;
    if ( !EnumProcessModulesEx( hProc , mods , sizeof( mods ) , &needed , LIST_MODULES_ALL ) ) return nullptr;
    DWORD count = needed / ( DWORD ) sizeof( HMODULE );
    if ( count > 1024 ) count = 1024;
    for ( DWORD i = 0; i < count; i++ ) {
        char modName[ MAX_PATH ] = {};
        if ( GetModuleBaseNameA( hProc , mods[ i ] , modName , sizeof( modName ) ) )
            if ( _stricmp( modName , name ) == 0 ) return mods[ i ];
    }
    return nullptr;
}

inline void* ResolveRemote_Stream( HANDLE hProc , const char* moduleName , const char* funcName ) {
    HMODULE local = GetModuleHandleA( moduleName );
    if ( !local ) local = LoadLibraryA( moduleName );
    if ( !local ) return nullptr;
    void* localFn = GetProcAddress( local , funcName );
    if ( !localFn ) return nullptr;
    uintptr_t rva = ( uintptr_t ) localFn - ( uintptr_t ) local;
    HMODULE remote = FindRemoteModule_Stream( hProc , moduleName );
    if ( !remote ) return nullptr;
    return ( void* ) ( ( uintptr_t ) remote + rva );
}

inline void* FindSlotAddrRemote_Stream( HANDLE hProc , HMODULE remoteBase , const char* sub1 , const char* sub2 ) {
    IMAGE_DOS_HEADER dos = {};
    if ( !ReadProcessMemory( hProc , remoteBase , &dos , sizeof( dos ) , nullptr ) ) return nullptr;
    IMAGE_NT_HEADERS64 nt = {};
    if ( !ReadProcessMemory( hProc , ( BYTE* ) remoteBase + dos.e_lfanew , &nt , sizeof( nt ) , nullptr ) ) return nullptr;
    DWORD expRva = nt.OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ].VirtualAddress;
    if ( !expRva ) return nullptr;
    IMAGE_EXPORT_DIRECTORY exp = {};
    if ( !ReadProcessMemory( hProc , ( BYTE* ) remoteBase + expRva , &exp , sizeof( exp ) , nullptr ) ) return nullptr;
    std::vector<DWORD> names( exp.NumberOfNames );
    std::vector<DWORD> funcs( exp.NumberOfFunctions );
    std::vector<WORD>  ords( exp.NumberOfNames );
    ReadProcessMemory( hProc , ( BYTE* ) remoteBase + exp.AddressOfNames        , names.data( ) , exp.NumberOfNames     * sizeof( DWORD ) , nullptr );
    ReadProcessMemory( hProc , ( BYTE* ) remoteBase + exp.AddressOfFunctions    , funcs.data( ) , exp.NumberOfFunctions * sizeof( DWORD ) , nullptr );
    ReadProcessMemory( hProc , ( BYTE* ) remoteBase + exp.AddressOfNameOrdinals , ords.data( )  , exp.NumberOfNames     * sizeof( WORD )  , nullptr );
    void* slot = nullptr;
    for ( DWORD i = 0; i < exp.NumberOfNames; i++ ) {
        char nm[ 256 ] = { 0 };
        ReadProcessMemory( hProc , ( BYTE* ) remoteBase + names[ i ] , nm , sizeof( nm ) - 1 , nullptr );
        if ( strstr( nm , sub1 ) && strstr( nm , sub2 ) ) {
            slot = ( void* ) ( ( BYTE* ) remoteBase + funcs[ ords[ i ] ] );
            break;
        }
    }
    return slot;
}

inline int PatchMagicRange_Stream( unsigned char* blob , size_t blobSize , uint64_t magicBase , size_t rangeSize , uint64_t newBase ) {
    int64_t delta = ( int64_t ) newBase - ( int64_t ) magicBase;
    int count = 0;
    for ( size_t i = 0; i + sizeof( uint64_t ) <= blobSize; i++ ) {
        uint64_t v = *( uint64_t* ) ( blob + i );
        if ( v >= magicBase && v < magicBase + rangeSize ) {
            *( uint64_t* ) ( blob + i ) = ( uint64_t ) ( ( int64_t ) v + delta );
            count++;
        }
    }
    return count;
}

// ─── HWND discovery ─────────────────────────────────────────────────────────
struct FindHwndCtx_Stream { DWORD pid; HWND hwnd; };
static BOOL CALLBACK FindMainProc_Stream( HWND h , LPARAM lp ) {
    auto* c = ( FindHwndCtx_Stream* ) lp;
    DWORD wpid = 0; GetWindowThreadProcessId( h , &wpid );
    if ( wpid == c->pid && GetWindow( h , GW_OWNER ) == nullptr && IsWindowVisible( h ) ) { c->hwnd = h; return FALSE; }
    return TRUE;
}
inline HWND FindMainHwnd_Stream( DWORD pid ) {
    FindHwndCtx_Stream c{ pid , nullptr };
    EnumWindows( FindMainProc_Stream , ( LPARAM ) &c );
    return c.hwnd;
}
struct BiggestCtx_Stream { HWND best; int bestArea; };
static BOOL CALLBACK BiggestChildProc_Stream( HWND h , LPARAM lp ) {
    auto* c = ( BiggestCtx_Stream* ) lp;
    if ( !IsWindowVisible( h ) ) return TRUE;
    RECT r; if ( !GetClientRect( h , &r ) ) return TRUE;
    int a = ( r.right - r.left ) * ( r.bottom - r.top );
    if ( a > c->bestArea ) { c->bestArea = a; c->best = h; }
    return TRUE;
}
inline HWND FindRenderHwnd_Stream( HWND parent ) {
    if ( !parent ) return nullptr;
    BiggestCtx_Stream c{ nullptr , 0 };
    EnumChildWindows( parent , BiggestChildProc_Stream , ( LPARAM ) &c );
    if ( !c.best ) return parent;
    RECT pr; GetClientRect( parent , &pr );
    int parentArea = ( pr.right - pr.left ) * ( pr.bottom - pr.top );
    if ( parentArea > 0 && c.bestArea * 100 / parentArea < 40 ) return parent;
    return c.best;
}

// ─── Injector ────────────────────────────────────────────────────────────────
class Injector {
public:
    ~Injector( ) { Uninstall( ); }

    bool Install( ) {
        if ( installed ) return false;

        // Pid: obtido diretamente de EMemory (seguro, sem nova snapshot de módulo).
        pid = EMemory::GetProcessPid( );
        if ( !pid ) return false;

        // Adquire handle com acesso COMPLETO (DUPLICATE_SAME_ACCESS) — sem Event ID 10.
        // Este handle pertence a nós; fechamos em Uninstall()/Fail().
        hProc = EMemory::AcquireChamsHandle( );
        if ( !hProc ) return false;

        HMODULE remoteRenderer = FindRemoteModule_Stream( hProc , STREAM_TARGET_MODULE );
        if ( !remoteRenderer ) return Fail( );

        slotAddr = FindSlotAddrRemote_Stream( hProc , remoteRenderer , STREAM_EAT_SUB1 , STREAM_EAT_SUB2 );
        if ( !slotAddr ) return Fail( );

        SIZE_T br;
        if ( !ReadProcessMemory( hProc , slotAddr , &originalDraw , sizeof( originalDraw ) , &br ) ) return Fail( );

        const size_t streamBufSize = sizeof( ChamsStream::StreamHeader ) + 2ULL * ChamsStream::CHAMS_STREAM_SLOT_BYTES;
        remoteStream = VirtualAllocEx( hProc , nullptr , streamBufSize , MEM_COMMIT | MEM_RESERVE , PAGE_READWRITE );
        if ( !remoteStream ) return Fail( );
        {
            std::vector<uint8_t> zeros( streamBufSize , 0 );
            WriteProcessMemory( hProc , remoteStream , zeros.data( ) , streamBufSize , &br );
        }

        ChamsStream::Context ctx = {};
        ctx.pglGetIntegerv     = ( ChamsStream::PFN_glGetIntegerv )     ResolveRemote_Stream( hProc , "opengl32.dll" , "glGetIntegerv" );
        ctx.pglEnable          = ( ChamsStream::PFN_glEnable )          ResolveRemote_Stream( hProc , "opengl32.dll" , "glEnable" );
        ctx.pglDisable         = ( ChamsStream::PFN_glDisable )         ResolveRemote_Stream( hProc , "opengl32.dll" , "glDisable" );
        ctx.pglColorMask       = ( ChamsStream::PFN_glColorMask )       ResolveRemote_Stream( hProc , "opengl32.dll" , "glColorMask" );
        ctx.pglBlendFunc       = ( ChamsStream::PFN_glBlendFunc )       ResolveRemote_Stream( hProc , "opengl32.dll" , "glBlendFunc" );
        ctx.pglDepthRange      = ( ChamsStream::PFN_glDepthRange )      ResolveRemote_Stream( hProc , "opengl32.dll" , "glDepthRange" );
        ctx.pwglGetProcAddress = ( ChamsStream::PFN_wglGetProcAddress ) ResolveRemote_Stream( hProc , "opengl32.dll" , "wglGetProcAddress" );
        ctx.pGetAsyncKeyState  = ( ChamsStream::PFN_GetAsyncKeyState )  ResolveRemote_Stream( hProc , "user32.dll"   , "GetAsyncKeyState" );
        ctx.pglGenTextures     = ( ChamsStream::PFN_glGenTextures )     ResolveRemote_Stream( hProc , "opengl32.dll" , "glGenTextures" );
        ctx.pglBindTexture     = ( ChamsStream::PFN_glBindTexture )     ResolveRemote_Stream( hProc , "opengl32.dll" , "glBindTexture" );
        ctx.pglTexImage2D      = ( ChamsStream::PFN_glTexImage2D )      ResolveRemote_Stream( hProc , "opengl32.dll" , "glTexImage2D" );
        ctx.pglTexParameteri   = ( ChamsStream::PFN_glTexParameteri )   ResolveRemote_Stream( hProc , "opengl32.dll" , "glTexParameteri" );
        ctx.pglReadPixels      = ( ChamsStream::PFN_glReadPixels )      ResolveRemote_Stream( hProc , "opengl32.dll" , "glReadPixels" );
        ctx.pglClear           = ( ChamsStream::PFN_glClear )           ResolveRemote_Stream( hProc , "opengl32.dll" , "glClear" );
        ctx.pglClearColor      = ( ChamsStream::PFN_glClearColor )      ResolveRemote_Stream( hProc , "opengl32.dll" , "glClearColor" );
        ctx.pglViewport        = ( ChamsStream::PFN_glViewport )        ResolveRemote_Stream( hProc , "opengl32.dll" , "glViewport" );
        ctx.pglDeleteTextures  = ( ChamsStream::PFN_glDeleteTextures )  ResolveRemote_Stream( hProc , "opengl32.dll" , "glDeleteTextures" );
        ctx.pGetTickCount      = ( ChamsStream::PFN_GetTickCount_t )    ResolveRemote_Stream( hProc , "kernel32.dll" , "GetTickCount" );

        if ( !ctx.pglGetIntegerv || !ctx.pglEnable || !ctx.pglDisable || !ctx.pglColorMask ||
            !ctx.pglBlendFunc || !ctx.pglDepthRange || !ctx.pwglGetProcAddress || !ctx.pGetAsyncKeyState ||
            !ctx.pglGenTextures || !ctx.pglBindTexture || !ctx.pglTexImage2D || !ctx.pglTexParameteri ||
            !ctx.pglReadPixels || !ctx.pglClear || !ctx.pglClearColor || !ctx.pglViewport ||
            !ctx.pglDeleteTextures || !ctx.pGetTickCount )
            return Fail( );

        ctx.pglGenFramebuffers      = nullptr;
        ctx.pglBindFramebuffer      = nullptr;
        ctx.pglFramebufferTexture2D = nullptr;
        ctx.pglDeleteFramebuffers   = nullptr;
        ctx.pOriginalDraw           = ( ChamsStream::PFN_glDrawElements ) originalDraw;
        ctx.pglGetUniformLocation   = nullptr;

        strcpy_s( ctx.sGetUniLoc              , "glGetUniformLocation" );
        strcpy_s( ctx.sCharaLight             , "_CharaLightIntensity" );
        strcpy_s( ctx.sGenFramebuffers        , "glGenFramebuffers" );
        strcpy_s( ctx.sBindFramebuffer        , "glBindFramebuffer" );
        strcpy_s( ctx.sFramebufferTexture2D   , "glFramebufferTexture2D" );
        strcpy_s( ctx.sDeleteFramebuffers     , "glDeleteFramebuffers" );
        strcpy_s( ctx.sGenRenderbuffers       , "glGenRenderbuffers" );
        strcpy_s( ctx.sBindRenderbuffer       , "glBindRenderbuffer" );
        strcpy_s( ctx.sRenderbufferStorage    , "glRenderbufferStorage" );
        strcpy_s( ctx.sFramebufferRenderbuffer, "glFramebufferRenderbuffer" );
        strcpy_s( ctx.sDeleteRenderbuffers    , "glDeleteRenderbuffers" );
        strcpy_s( ctx.sBlitFramebuffer        , "glBlitFramebuffer" );

        ctx.chamsState    = 1;
        ctx.inDrawHook    = 0;
        ctx.streamMode    = 1;
        ctx.fboInit       = 0;
        ctx.unloadPending = 0;
        ctx.pStreamBuf    = remoteStream;

        remoteCtx = VirtualAllocEx( hProc , nullptr , sizeof( ChamsStream::Context ) , MEM_COMMIT | MEM_RESERVE , PAGE_READWRITE );
        if ( !remoteCtx ) return Fail( );
        if ( !WriteProcessMemory( hProc , remoteCtx , &ctx , sizeof( ctx ) , &br ) ) return Fail( );

        unsigned char* blob = new unsigned char[ sizeof( g_streamShellcode ) ];
        memcpy( blob , g_streamShellcode , sizeof( g_streamShellcode ) );
        int patches = PatchMagicRange_Stream( blob , sizeof( g_streamShellcode ) , ChamsStream::CTX_MAGIC , sizeof( ChamsStream::Context ) , ( uint64_t ) remoteCtx );
        if ( patches < 1 ) { delete[] blob; return Fail( ); }

        remoteCode = VirtualAllocEx( hProc , nullptr , sizeof( g_streamShellcode ) , MEM_COMMIT | MEM_RESERVE , PAGE_EXECUTE_READ );
        if ( !remoteCode ) { delete[] blob; return Fail( ); }

        DWORD oldProt;
        if ( !VirtualProtectEx( hProc , remoteCode , sizeof( g_streamShellcode ) , PAGE_READWRITE , &oldProt ) ) { delete[] blob; return Fail( ); }
        if ( !WriteProcessMemory( hProc , remoteCode , blob , sizeof( g_streamShellcode ) , &br ) )              { delete[] blob; return Fail( ); }
        VirtualProtectEx( hProc , remoteCode , sizeof( g_streamShellcode ) , PAGE_EXECUTE_READ , &oldProt );
        delete[] blob;

        if ( !VirtualProtectEx( hProc , slotAddr , sizeof( void* ) , PAGE_READWRITE , &oldProt ) ) return Fail( );
        if ( !WriteProcessMemory( hProc , slotAddr , &remoteCode , sizeof( remoteCode ) , &br ) )  return Fail( );
        VirtualProtectEx( hProc , slotAddr , sizeof( void* ) , oldProt , &oldProt );

        installed = true;
        return true;
    }

    void Uninstall( ) {
        if ( !installed ) {
            if ( hProc ) { CloseHandle( hProc ); hProc = nullptr; }
            return;
        }

        SIZE_T br;
        ChamsStream::Context* rc = ( ChamsStream::Context* ) remoteCtx;
        LONG v0 = 0, v1 = 1;

        if ( remoteCtx && hProc ) WriteProcessMemory( hProc , ( LPVOID ) &rc->streamMode , &v0 , sizeof( v0 ) , &br );
        if ( remoteCtx && hProc ) WriteProcessMemory( hProc , ( LPVOID ) &rc->chamsState , &v0 , sizeof( v0 ) , &br );
        Sleep( 500 );

        if ( remoteCtx && hProc ) {
            WriteProcessMemory( hProc , ( LPVOID ) &rc->unloadPending , &v1 , sizeof( v1 ) , &br );
            DWORD timeout = GetTickCount( ) + 1000;
            for ( ;; ) {
                LONG state = 0;
                if ( !ReadProcessMemory( hProc , ( LPCVOID ) &rc->unloadPending , &state , sizeof( state ) , &br ) ) break;
                if ( state == 2 ) break;
                if ( GetTickCount( ) > timeout ) break;
                Sleep( 10 );
            }
        }

        DWORD oldProt;
        if ( slotAddr && hProc && VirtualProtectEx( hProc , slotAddr , sizeof( void* ) , PAGE_READWRITE , &oldProt ) ) {
            WriteProcessMemory( hProc , slotAddr , &originalDraw , sizeof( originalDraw ) , &br );
            VirtualProtectEx( hProc , slotAddr , sizeof( void* ) , oldProt , &oldProt );
        }

        if ( remoteCtx && hProc ) {
            DWORD timeout = GetTickCount( ) + 3000;
            for ( ;; ) {
                LONG inHook = 0;
                if ( !ReadProcessMemory( hProc , ( LPCVOID ) &rc->inDrawHook , &inHook , sizeof( inHook ) , &br ) ) break;
                if ( inHook == 0 ) break;
                if ( GetTickCount( ) > timeout ) break;
                Sleep( 10 );
            }
        }

        if ( remoteCode && hProc ) {
            std::vector<uint8_t> z( sizeof( g_streamShellcode ) , 0 );
            DWORD op;
            if ( VirtualProtectEx( hProc , remoteCode , sizeof( g_streamShellcode ) , PAGE_READWRITE , &op ) )
                WriteProcessMemory( hProc , remoteCode , z.data( ) , z.size( ) , &br );
            VirtualFreeEx( hProc , remoteCode , 0 , MEM_RELEASE );
            remoteCode = nullptr;
        }
        if ( remoteCtx && hProc ) {
            ChamsStream::Context z = {};
            WriteProcessMemory( hProc , remoteCtx , &z , sizeof( z ) , &br );
            VirtualFreeEx( hProc , remoteCtx , 0 , MEM_RELEASE );
            remoteCtx = nullptr;
        }
        if ( remoteStream && hProc ) {
            const size_t sz = sizeof( ChamsStream::StreamHeader ) + 2ULL * ChamsStream::CHAMS_STREAM_SLOT_BYTES;
            std::vector<uint8_t> z( sz , 0 );
            WriteProcessMemory( hProc , remoteStream , z.data( ) , z.size( ) , &br );
            VirtualFreeEx( hProc , remoteStream , 0 , MEM_RELEASE );
            remoteStream = nullptr;
        }

        // Fecha o handle — é nosso (adquirido via AcquireChamsHandle).
        if ( hProc ) { CloseHandle( hProc ); hProc = nullptr; }
        slotAddr = nullptr;
        originalDraw = nullptr;
        pid = 0;
        installed = false;
    }

    bool   IsInstalled( ) const { return installed; }
    HANDLE Handle( )      const { return hProc; }
    void*  Ctx( )         const { return remoteCtx; }
    void*  StreamBuf( )   const { return remoteStream; }
    DWORD  Pid( )         const { return pid; }

private:
    bool   installed    = false;
    DWORD  pid          = 0;
    HANDLE hProc        = nullptr;
    void*  slotAddr     = nullptr;
    void*  originalDraw = nullptr;
    void*  remoteCtx    = nullptr;
    void*  remoteCode   = nullptr;
    void*  remoteStream = nullptr;

    bool Fail( ) {
        if ( remoteCode   && hProc ) { VirtualFreeEx( hProc , remoteCode   , 0 , MEM_RELEASE ); remoteCode   = nullptr; }
        if ( remoteCtx    && hProc ) { VirtualFreeEx( hProc , remoteCtx    , 0 , MEM_RELEASE ); remoteCtx    = nullptr; }
        if ( remoteStream && hProc ) { VirtualFreeEx( hProc , remoteStream , 0 , MEM_RELEASE ); remoteStream = nullptr; }
        // Fecha o handle — é nosso (adquirido via AcquireChamsHandle).
        if ( hProc ) { CloseHandle( hProc ); hProc = nullptr; }
        slotAddr = nullptr;
        originalDraw = nullptr;
        pid = 0;
        installed = false;
        return false;
    }
};

// ─── Overlay externa (WDA_EXCLUDEFROMCAPTURE + LWA_COLORKEY + StretchBlt) ────
class Overlay {
public:
    bool Create( HINSTANCE hInst ) {
        WNDCLASSEXW wc = { sizeof( wc ) };
        wc.style         = 0;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = L"ChamsStreamOverlay";
        wc.hCursor       = LoadCursorW( nullptr , IDC_ARROW );
        wc.hbrBackground = ( HBRUSH ) GetStockObject( BLACK_BRUSH );
        RegisterClassExW( &wc );

        hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED ,
            wc.lpszClassName , L" " ,
            WS_POPUP ,
            0 , 0 , 100 , 100 ,
            nullptr , nullptr , hInst , nullptr );
        if ( !hwnd ) return false;

        SetLayeredWindowAttributes( hwnd , RGB( 0 , 0 , 0 ) , 255 , LWA_COLORKEY );
        // StreamMode usermode via WDA_EXCLUDEFROMCAPTURE — invisível a OBS/Discord/screenshots.
        SetWindowDisplayAffinity( hwnd , WDA_EXCLUDEFROMCAPTURE );
        ShowWindow( hwnd , SW_SHOWNA );
        return true;
    }

    void SyncTo( HWND target ) {
        if ( !target || !IsWindow( target ) ) return;
        RECT rc;
        if ( !GetClientRect( target , &rc ) ) return;
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if ( w < 1 || h < 1 ) return;
        POINT tl = { 0, 0 };
        ClientToScreen( target , &tl );

        if ( w != winW || h != winH || tl.x != winX || tl.y != winY ) {
            winW = w; winH = h; winX = tl.x; winY = tl.y;
            SetWindowPos( hwnd , HWND_TOPMOST , tl.x , tl.y , w , h ,
                SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS | SWP_SHOWWINDOW );
        }
    }

    void Render( const uint8_t* srcRgba , int srcW , int srcH ) {
        if ( !srcRgba || srcW < 1 || srcH < 1 ) { Clear( ); return; }
        if ( srcW != dibW || srcH != dibH ) EnsureDib( srcW , srcH );
        if ( !dibBits ) return;

        for ( int y = 0; y < srcH; ++y ) {
            int sy = srcH - 1 - y;
            const uint32_t* srcRow = ( const uint32_t* ) srcRgba + sy * srcW;
            uint32_t*       dstRow = ( uint32_t* ) dibBits + y * srcW;
            for ( int x = 0; x < srcW; ++x ) {
                uint32_t p = srcRow[ x ];
                uint32_t r =   p         & 0xFFu;
                uint32_t g = ( p >>  8 ) & 0xFFu;
                uint32_t b = ( p >> 16 ) & 0xFFu;
                if ( ( r | g | b ) == 0 ) { dstRow[ x ] = 0; continue; }
                dstRow[ x ] = b | ( g << 8 ) | ( r << 16 ) | 0xFF000000u;
            }
        }
        Present( );
    }

    void Clear( ) {
        if ( !hwnd || winW < 1 || winH < 1 ) return;
        HDC winDC = GetDC( hwnd );
        if ( winDC ) {
            RECT r = { 0, 0, winW, winH };
            FillRect( winDC , &r , ( HBRUSH ) GetStockObject( BLACK_BRUSH ) );
            ReleaseDC( hwnd , winDC );
        }
    }

    bool ReapplyDisplayAffinity( ) {
        if ( !hwnd ) return false;
        SetWindowDisplayAffinity( hwnd , WDA_EXCLUDEFROMCAPTURE );
        return true;
    }

    void Destroy( ) {
        if ( hwnd ) SetWindowDisplayAffinity( hwnd , WDA_NONE );
        if ( dib )   { DeleteObject( dib );   dib = nullptr; }
        if ( memDC ) { DeleteDC( memDC );     memDC = nullptr; }
        if ( hwnd )  { DestroyWindow( hwnd ); hwnd = nullptr; }
        dibBits = nullptr; dibW = dibH = 0;
        winW = winH = 0; winX = winY = INT_MIN;
    }

    ~Overlay( ) { Destroy( ); }

private:
    HWND    hwnd    = nullptr;
    HDC     memDC   = nullptr;
    HBITMAP dib     = nullptr;
    void*   dibBits = nullptr;
    int     dibW    = 0;
    int     dibH    = 0;
    int     winW    = 0;
    int     winH    = 0;
    int     winX    = INT_MIN;
    int     winY    = INT_MIN;

    void EnsureDib( int w , int h ) {
        if ( dib )   { DeleteObject( dib );  dib = nullptr; dibBits = nullptr; }
        if ( memDC ) { DeleteDC( memDC );    memDC = nullptr; }
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC( nullptr );
        memDC = CreateCompatibleDC( screen );
        dib   = CreateDIBSection( screen , &bi , DIB_RGB_COLORS , &dibBits , nullptr , 0 );
        ReleaseDC( nullptr , screen );
        if ( memDC && dib ) SelectObject( memDC , dib );
        dibW = w; dibH = h;
    }

    void Present( ) {
        if ( !hwnd || !memDC || dibW < 1 || dibH < 1 || winW < 1 || winH < 1 ) return;
        HDC winDC = GetDC( hwnd );
        if ( !winDC ) return;
        if ( winW == dibW && winH == dibH ) {
            BitBlt( winDC , 0 , 0 , winW , winH , memDC , 0 , 0 , SRCCOPY );
        } else {
            SetStretchBltMode( winDC , COLORONCOLOR );
            StretchBlt( winDC , 0 , 0 , winW , winH , memDC , 0 , 0 , dibW , dibH , SRCCOPY );
        }
        ReleaseDC( hwnd , winDC );
    }

    static LRESULT CALLBACK WndProc( HWND h , UINT m , WPARAM w , LPARAM l ) {
        return DefWindowProcW( h , m , w , l );
    }
};

// ─── Controlador ─────────────────────────────────────────────────────────────
class Controller {
public:
    static Controller& Get( ) { static Controller c; return c; }

    bool Enable( ) {
        std::lock_guard<std::mutex> lk( m_ctrl );
        if ( m_active.load( ) ) return true;

        if ( !m_inj.Install( ) ) return false;

        if ( !m_overlay.Create( GetModuleHandleW( nullptr ) ) ) {
            m_inj.Uninstall( );
            return false;
        }

        m_stopFlag.store( false );
        m_active.store( true );
        m_worker = std::thread( &Controller::RenderLoop , this );
        return true;
    }

    void Disable( ) {
        std::lock_guard<std::mutex> lk( m_ctrl );
        if ( !m_active.load( ) ) return;

        m_stopFlag.store( true );
        if ( m_worker.joinable( ) ) m_worker.join( );

        m_overlay.Clear( );
        m_overlay.Destroy( );
        m_inj.Uninstall( );
        m_active.store( false );
    }

    bool IsActive( ) const { return m_active.load( ); }

    ~Controller( ) { Disable( ); }

private:
    Controller( ) = default;
    Controller( const Controller& ) = delete;
    Controller& operator=( const Controller& ) = delete;

    void RenderLoop( ) {
        SetThreadPriority( GetCurrentThread( ) , THREAD_PRIORITY_BELOW_NORMAL );

        HWND topHwnd  = FindMainHwnd_Stream( m_inj.Pid( ) );
        HWND gameHwnd = FindRenderHwnd_Stream( topHwnd );

        LONG  lastSeq = -1;
        DWORD lastFreshTick = 0;
        DWORD lastAffinityReapply = 0;
        std::vector<uint8_t> pixBuf( ChamsStream::CHAMS_STREAM_SLOT_BYTES );

        while ( !m_stopFlag.load( ) ) {
            if ( !m_inj.IsInstalled( ) || !m_inj.Handle( ) ) break;

            if ( !topHwnd || !IsWindow( topHwnd ) ) {
                topHwnd  = FindMainHwnd_Stream( m_inj.Pid( ) );
                gameHwnd = FindRenderHwnd_Stream( topHwnd );
            }

            DWORD now = GetTickCount( );
            if ( now - lastAffinityReapply > 2000 ) {
                m_overlay.ReapplyDisplayAffinity( );
                lastAffinityReapply = now;
            }

            if ( gameHwnd ) {
                m_overlay.SyncTo( gameHwnd );

                SIZE_T br;
                ChamsStream::StreamHeader hdr;
                if ( ReadProcessMemory( m_inj.Handle( ) , m_inj.StreamBuf( ) , &hdr , sizeof( hdr ) , &br ) && br == sizeof( hdr ) ) {
                    if ( hdr.frameSeq != lastSeq
                        && hdr.width > 0 && hdr.height > 0
                        && hdr.width <= ChamsStream::CHAMS_STREAM_MAX_W
                        && hdr.height <= ChamsStream::CHAMS_STREAM_MAX_H
                        && ( hdr.activeSlot == 0 || hdr.activeSlot == 1 ) ) {
                        size_t pxBytes = ( size_t ) hdr.width * hdr.height * 4;
                        if ( pxBytes > pixBuf.size( ) ) pixBuf.resize( pxBytes );
                        void* srcAddr = ( uint8_t* ) m_inj.StreamBuf( ) + sizeof( ChamsStream::StreamHeader )
                            + ( size_t ) hdr.activeSlot * ChamsStream::CHAMS_STREAM_SLOT_BYTES;
                        if ( ReadProcessMemory( m_inj.Handle( ) , srcAddr , pixBuf.data( ) , pxBytes , &br ) && br == pxBytes ) {
                            m_overlay.Render( pixBuf.data( ) , hdr.width , hdr.height );
                            lastSeq = hdr.frameSeq;
                            lastFreshTick = now;
                        }
                    }
                    else if ( lastFreshTick != 0 && now - lastFreshTick > 500 ) {
                        m_overlay.Clear( );
                        lastFreshTick = 0;
                    }
                }
            }

            Cheat::HiRes::SleepMillis( 8 );
        }
    }

    Injector           m_inj;
    Overlay            m_overlay;
    std::thread        m_worker;
    std::atomic<bool>  m_active   { false };
    std::atomic<bool>  m_stopFlag { false };
    std::mutex         m_ctrl;
};

} // namespace Stream
} // namespace Chams
