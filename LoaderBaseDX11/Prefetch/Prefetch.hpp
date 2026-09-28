#pragma once
#include <Windows.h>
#include <string.h>   // memcpy (intrinsic)

// wmemcpy inline — não depende de CRT init; memcpy é compiler intrinsic
static inline void pf_wmemcpy(wchar_t* dst, const wchar_t* src, int n) {
    memcpy(dst, src, (size_t)n * sizeof(wchar_t));
}

// ─────────────────────────────────────────────────────────────────────────────
// Prefetch.hpp — reescrito 100% WinAPI, sem STL.
//
// Motivo: em contexto de manual-map injection os construtores C++ (.CRT$XCU)
// não são chamados. Qualquer objeto STL com construtor não-trivial (std::vector,
// std::wstring, std::unordered_map, fs::directory_iterator, static T instance
// com guard de thread-safe) causa Access Violation na primeira chamada.
//
// Esta versão usa apenas HeapAlloc/FindFirstFileW/WinAPI, sem guard variables
// nem mutex do CRT. Todos os globals são POD (zero-initialized pelo VirtualAlloc).
// ─────────────────────────────────────────────────────────────────────────────

// ── Helpers de string (sem CRT) ───────────────────────────────────────────────

static inline wchar_t pf_toLowerW(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? (c + 32) : c;
}

// Case-insensitive: retorna true se [name, name+nameLen) contém [sub, sub+subLen)
static bool pf_wContains(const wchar_t* name, int nameLen,
                          const wchar_t* sub,  int subLen)
{
    if (subLen > nameLen) return false;
    for (int i = 0; i <= nameLen - subLen; ++i) {
        bool ok = true;
        for (int j = 0; j < subLen; ++j) {
            if (pf_toLowerW(name[i+j]) != sub[j]) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

// Retorna true se o nome (wchar_t*, len chars) identifica uma DLL DirectX.
// sub[] já em minúsculas — comparação com pf_toLowerW é case-insensitive.
static bool pf_isDirectX(const wchar_t* name, int len) {
    return pf_wContains(name, len, L"d3d",    3) ||
           pf_wContains(name, len, L"dxgi",   4) ||
           pf_wContains(name, len, L"dxva",   4) ||
           pf_wContains(name, len, L"dinput", 6) ||
           pf_wContains(name, len, L"xinput", 6) ||
           pf_wContains(name, len, L"xaudio", 6) ||
           pf_wContains(name, len, L"d2d1",   4);
}

// wcslen sem CRT
static int pf_wlen(const wchar_t* s) {
    int n = 0; while (s[n]) ++n; return n;
}

// ── Cache de DLLs do System32 por comprimento ─────────────────────────────────
// Mapeia: comprimento do nome (wchar_t) → { primary, alternate }
// sem nenhum nome DirectX. Construído via FindFirstFileW uma única vez.
// Todos os campos são POD/zero-initialized — sem necessidade de construtor.

#define PF_MAX_LEN_ENTRIES 80

struct pf_LenEntry {
    DWORD len;
    wchar_t primary[64];
    wchar_t alternate[64];
};

// Statics POD: zero-initialized pelo VirtualAlloc — sem construtor necessário.
static pf_LenEntry  s_pf_lenCache[PF_MAX_LEN_ENTRIES];
static int          s_pf_lenCount  = 0;
// 0 = não construído; 1 = em construção; 2 = pronto
static LONG         s_pf_builtFlag = 0;

static void pf_buildLenCache() {
    // Garante execução única sem mutex do CRT
    if (InterlockedCompareExchange(&s_pf_builtFlag, 1, 0) != 0) {
        // Outro thread está construindo — espera
        while (s_pf_builtFlag == 1) Sleep(1);
        return;
    }

    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW(L"C:\\Windows\\System32\\*.dll", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            int n = pf_wlen(fd.cFileName);
            if (n < 4 || n >= 64) continue;
            if (pf_isDirectX(fd.cFileName, n)) continue;

            // Procura slot para este comprimento
            int idx = -1;
            for (int i = 0; i < s_pf_lenCount; ++i) {
                if (s_pf_lenCache[i].len == (DWORD)n) { idx = i; break; }
            }
            if (idx < 0 && s_pf_lenCount < PF_MAX_LEN_ENTRIES) {
                idx = s_pf_lenCount++;
                s_pf_lenCache[idx].len         = (DWORD)n;
                s_pf_lenCache[idx].primary[0]  = 0;
                s_pf_lenCache[idx].alternate[0] = 0;
            }
            if (idx >= 0) {
                if (!s_pf_lenCache[idx].primary[0]) {
                    for (int i = 0; i < n; ++i)
                        s_pf_lenCache[idx].primary[i] = pf_toLowerW(fd.cFileName[i]);
                    s_pf_lenCache[idx].primary[n] = 0;
                } else if (!s_pf_lenCache[idx].alternate[0]) {
                    for (int i = 0; i < n; ++i)
                        s_pf_lenCache[idx].alternate[i] = pf_toLowerW(fd.cFileName[i]);
                    s_pf_lenCache[idx].alternate[n] = 0;
                }
            }
        } while (FindNextFileW(h, &fd) && s_pf_lenCount < PF_MAX_LEN_ENTRIES);
        FindClose(h);
    }
    InterlockedExchange(&s_pf_builtFlag, 2);
}

static const pf_LenEntry* pf_getLenEntry(DWORD len) {
    if (s_pf_builtFlag != 2) pf_buildLenCache();
    for (int i = 0; i < s_pf_lenCount; ++i)
        if (s_pf_lenCache[i].len == len) return &s_pf_lenCache[i];
    return NULL;
}

// ── I/O de arquivo (WinAPI puro) ──────────────────────────────────────────────

static BYTE* pf_readFile(const wchar_t* path, DWORD* outSize) {
    HANDLE h = CreateFileW(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz < 0x100) { CloseHandle(h); return NULL; }
    BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, sz);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD got = 0;
    if (!ReadFile(h, buf, sz, &got, NULL) || got != sz) {
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    *outSize = sz;
    return buf;
}

// ── Descompressão (formato MAM / XPRESS_HUFF) ─────────────────────────────────

static BYTE* pf_decompress(const BYTE* data, DWORD dataSize, DWORD* outDecompSize) {
    using fnGetWS  = NTSTATUS(__stdcall*)(USHORT, PULONG, PULONG);
    using fnDecomp = NTSTATUS(__stdcall*)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG, PVOID);

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll || dataSize < 8) return NULL;

    DWORD sig      = *(DWORD*)(data);
    if ((sig & 0x00FFFFFF) != 0x004D414D) return NULL; // "MAM"
    DWORD decompSz = *(DWORD*)(data + 4);
    USHORT fmt     = (USHORT)((sig & 0x0F000000) >> 24);

    auto fnGWS = (fnGetWS)GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize");
    auto fnDec = (fnDecomp)GetProcAddress(ntdll, "RtlDecompressBufferEx");
    if (!fnGWS || !fnDec) return NULL;

    ULONG wsSize = 0, fragSize = 0;
    if (fnGWS(fmt, &wsSize, &fragSize) != 0) return NULL;

    BYTE* ws  = (BYTE*)HeapAlloc(GetProcessHeap(), 0, wsSize);
    BYTE* out = (BYTE*)HeapAlloc(GetProcessHeap(), 0, decompSz);
    if (!ws || !out) {
        if (ws)  HeapFree(GetProcessHeap(), 0, ws);
        if (out) HeapFree(GetProcessHeap(), 0, out);
        return NULL;
    }

    ULONG finalSz = 0;
    NTSTATUS st = fnDec(fmt, out, decompSz,
                        (PUCHAR)(data + 8), dataSize - 8,
                        &finalSz, ws);
    HeapFree(GetProcessHeap(), 0, ws);
    if (st != 0) { HeapFree(GetProcessHeap(), 0, out); return NULL; }

    *outDecompSize = finalSz;
    return out;
}

// ── Compressão e gravação (formato MAM / XPRESS_HUFF) ─────────────────────────

static bool pf_compressAndSave(const wchar_t* path, BYTE* decompData, DWORD decompSz) {
    using fnGetWS  = NTSTATUS(__stdcall*)(USHORT, PULONG, PULONG);
    using fnComp   = NTSTATUS(__stdcall*)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, ULONG, PULONG, PVOID);

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return false;

    auto fnGWS = (fnGetWS)GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize");
    auto fnCmp = (fnComp) GetProcAddress(ntdll, "RtlCompressBuffer");
    if (!fnGWS || !fnCmp) return false;

    ULONG wsSize = 0, fragSize = 0;
    if (fnGWS(COMPRESSION_FORMAT_XPRESS_HUFF, &wsSize, &fragSize) != 0) return false;

    BYTE* ws     = (BYTE*)HeapAlloc(GetProcessHeap(), 0, wsSize);
    DWORD outCap = decompSz * 2 + 32;
    BYTE* outBuf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, outCap);
    if (!ws || !outBuf) {
        if (ws)     HeapFree(GetProcessHeap(), 0, ws);
        if (outBuf) HeapFree(GetProcessHeap(), 0, outBuf);
        return false;
    }

    ULONG compSz = 0;
    NTSTATUS st = fnCmp(COMPRESSION_FORMAT_XPRESS_HUFF,
                        decompData, decompSz,
                        outBuf, outCap, 0,
                        &compSz, ws);
    HeapFree(GetProcessHeap(), 0, ws);
    if (st != 0) { HeapFree(GetProcessHeap(), 0, outBuf); return false; }

    // Cabeçalho: [sig 4B] [decompSz 4B] [dados comprimidos]
    // sig = 0x044D414D  (bytes: 4D 41 4D 04 = "MAM\x04")
    DWORD   total   = 8 + compSz;
    BYTE*   final   = (BYTE*)HeapAlloc(GetProcessHeap(), 0, total);
    if (!final) { HeapFree(GetProcessHeap(), 0, outBuf); return false; }

    DWORD sig = 0x044D414D;
    memcpy(final,     &sig,    4);
    memcpy(final + 4, &decompSz, 4);
    memcpy(final + 8, outBuf, compSz);
    HeapFree(GetProcessHeap(), 0, outBuf);

    HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    bool ok = false;
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ok = WriteFile(hf, final, total, &written, NULL) && (written == total);
        CloseHandle(hf);
    }
    HeapFree(GetProcessHeap(), 0, final);
    return ok;
}

// ── Getters de offset da seção de filename strings ────────────────────────────

static inline DWORD pf_stringsOffset(const BYTE* d) { DWORD v; memcpy(&v, d+0x64, 4); return v; }
static inline DWORD pf_stringsSize  (const BYTE* d) { DWORD v; memcpy(&v, d+0x68, 4); return v; }

// ── Replace patches fixos (case-insensitive, mesmo tamanho) ──────────────────
// Substitui toda ocorrência de findW (já em lowercase, len=findLen) por replW
// em cada entrada da seção de filename strings. Retorna true se mudou algo.

static bool pf_replaceFixed(BYTE* data, DWORD dataSz,
                             const wchar_t* findW, int findLen,
                             const wchar_t* replW)
{
    DWORD off = pf_stringsOffset(data);
    DWORD sz  = pf_stringsSize(data);
    if (sz == 0 || off + sz > dataSz) return false;

    wchar_t* begin = (wchar_t*)(data + off);
    wchar_t* end   = (wchar_t*)(data + off + sz);

    bool     changed    = false;
    wchar_t* entryStart = begin;

    for (wchar_t* ptr = begin; ptr < end; ++ptr) {
        if (*ptr == L'\0') {
            int entryLen = (int)(ptr - entryStart);
            if (entryLen >= findLen) {
                for (int i = 0; i <= entryLen - findLen; ++i) {
                    bool match = true;
                    for (int j = 0; j < findLen; ++j) {
                        if (pf_toLowerW(entryStart[i+j]) != findW[j]) { match = false; break; }
                    }
                    if (match) {
                        pf_wmemcpy(entryStart + i, replW, findLen);
                        changed = true;
                    }
                }
            }
            entryStart = ptr + 1;
        }
    }
    return changed;
}

// ── Replace catch-all para DLLs DirectX não cobertas pelos patches fixos ─────

static bool pf_replaceDirectX(BYTE* data, DWORD dataSz) {
    DWORD off = pf_stringsOffset(data);
    DWORD sz  = pf_stringsSize(data);
    if (sz == 0 || off + sz > dataSz) return false;

    wchar_t* begin = (wchar_t*)(data + off);
    wchar_t* end   = (wchar_t*)(data + off + sz);

    // Rastreia se primary já foi usado para cada entrada do cache (evita duplicatas no mesmo .pf)
    BYTE usedPrimary[PF_MAX_LEN_ENTRIES] = {};

    bool     changed    = false;
    wchar_t* entryStart = begin;

    for (wchar_t* ptr = begin; ptr < end; ++ptr) {
        if (*ptr == L'\0') {
            int entryLen = (int)(ptr - entryStart);
            if (entryLen > 0) {
                // Extrai filename (após última '\')
                wchar_t* fnameStart = entryStart;
                for (wchar_t* p = entryStart; p < ptr; ++p)
                    if (*p == L'\\') fnameStart = p + 1;
                int fnameLen = (int)(ptr - fnameStart);

                if (fnameLen > 0 && pf_isDirectX(fnameStart, fnameLen)) {
                    const pf_LenEntry* le = pf_getLenEntry((DWORD)fnameLen);
                    if (le && le->primary[0]) {
                        // Encontra índice no cache para controle de usedPrimary
                        int cIdx = -1;
                        for (int i = 0; i < s_pf_lenCount; ++i) {
                            if (s_pf_lenCache[i].len == (DWORD)fnameLen) { cIdx = i; break; }
                        }

                        const wchar_t* repl = NULL;
                        if (cIdx >= 0 && !usedPrimary[cIdx]) {
                            repl = le->primary;
                            usedPrimary[cIdx] = 1;
                        } else if (le->alternate[0]) {
                            repl = le->alternate;
                        } else {
                            repl = le->primary; // único disponível
                        }

                        if (repl && pf_wlen(repl) == fnameLen) {
                            pf_wmemcpy(fnameStart, repl, fnameLen);
                            changed = true;
                        }
                    }
                }
            }
            entryStart = ptr + 1;
        }
    }
    return changed;
}

// ── Tabela de patches fixos ───────────────────────────────────────────────────
// findW já em lowercase (comparação é case-insensitive via pf_toLowerW).
// replW pode ter qualquer casing; comprimento == comprimento do findW garantido.

struct pf_Patch { const wchar_t* findW; const wchar_t* replW; };

static const pf_Patch k_pf_patches[] = {
    // DirectX core
    { L"d3d9.dll",            L"avrt.dll"           },
    { L"d3d10.dll",           L"aclui.dll"          },
    { L"d3d11.dll",           L"winmm.dll"          },
    { L"d3d12.dll",           L"imm32.dll"          },
    { L"dxgi.dll",            L"qmgr.dll"           },
    { L"dxva2.dll",           L"authz.dll"          },
    { L"d3d10_1.dll",         L"advpack.dll"        },
    { L"dinput8.dll",         L"shlwapi.dll"        },
    { L"dinput.dll",          L"rpcrt4.dll"         },
    { L"d3dx9_43.dll",        L"schannel.dll"       },
    { L"d3dx10_43.dll",       L"cryptbase.dll"      },
    { L"d3dx11_43.dll",       L"authfwcfg.dll"      },
    { L"xinput1_3.dll",       L"amsiproxy.dll"      },
    { L"xinput1_4.dll",       L"adtschema.dll"      },
    { L"xaudio2_9.dll",       L"adrclient.dll"      },
    { L"xaudio2_7.dll",       L"appraiser.dll"      },
    { L"d3dcompiler_43.dll",  L"authentication.dll" },
    { L"d3dcompiler_47.dll",  L"BcastDVRCommon.dll" },
    // Artefatos do cheat
    { L"fxstiffdebuglogfile.txt",             L"PortableDeviceTypes.dll"            },
    { L"vfcompat.dll",                        L"wintrust.dll"                       },
    { L"ffmpeg.dll",                          L"ffxpeg.dll"                         },
    { L"winsecpkg.dll",                       L"winsexpkg.dll"                      },
    { L"ncobjapi.dll",                        L"ncxbjapi.dll"                       },
    { L"citizenfx.ini",                       L"citizenfy.ini"                      },
    { L"scope_v3.json",                       L"scope_v3.jxon"                      },
    { L"a9f3g2h8k1m4.tmp.node",               L"a9f3g2h8k1m4.tmp.data"              },
    { L"filesync.localizeresources.dll.mui",  L"filesync.xocalizeresources.dll.mui" },
    { L"winsockt.dll",                        L"ncryptpk.dll"                       },
};
static const int k_pf_patchCount = (int)(sizeof(k_pf_patches) / sizeof(k_pf_patches[0]));

// ── Ponto de entrada principal ─────────────────────────────────────────────────
// Seguro em qualquer contexto (manual-map, loader normal, EXE).

inline int PrefetchSystemWinAPI() {
    int modified = 0;

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(L"C:\\Windows\\Prefetch\\*.pf", &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        // Monta caminho completo sem snprintf
        wchar_t fullPath[MAX_PATH];
        const wchar_t prefix[] = L"C:\\Windows\\Prefetch\\";
        int pLen = 20; // comprimento de prefix sem null
        int nLen = pf_wlen(fd.cFileName);
        if (pLen + nLen >= MAX_PATH) continue;
        pf_wmemcpy(fullPath, prefix, pLen);
        pf_wmemcpy(fullPath + pLen, fd.cFileName, nLen + 1);

        // Lê arquivo
        DWORD rawSz = 0;
        BYTE* rawBuf = pf_readFile(fullPath, &rawSz);
        if (!rawBuf) continue;

        BYTE* work   = NULL;
        DWORD workSz = 0;

        if (rawSz >= 3 && rawBuf[0]=='M' && rawBuf[1]=='A' && rawBuf[2]=='M') {
            // Formato comprimido MAM
            work = pf_decompress(rawBuf, rawSz, &workSz);
            HeapFree(GetProcessHeap(), 0, rawBuf);
            if (!work) continue;
        } else if (rawSz >= 8 && rawBuf[4]=='S' && rawBuf[5]=='C' &&
                                  rawBuf[6]=='C' && rawBuf[7]=='A') {
            // Formato SCCA (não comprimido)
            work   = rawBuf;
            workSz = rawSz;
            rawBuf = NULL; // transfere propriedade
        } else {
            HeapFree(GetProcessHeap(), 0, rawBuf);
            continue;
        }

        if (workSz < 0x100) { HeapFree(GetProcessHeap(), 0, work); continue; }

        // Aplica patches
        bool changed = false;
        for (int i = 0; i < k_pf_patchCount; ++i) {
            int findLen = pf_wlen(k_pf_patches[i].findW);
            changed |= pf_replaceFixed(work, workSz,
                                       k_pf_patches[i].findW, findLen,
                                       k_pf_patches[i].replW);
        }
        changed |= pf_replaceDirectX(work, workSz);

        if (changed) {
            pf_compressAndSave(fullPath, work, workSz);
            ++modified;
        }

        HeapFree(GetProcessHeap(), 0, work);

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return modified;
}

// ── Wrapper de classe para manter compatibilidade com o call site existente ────
// Classe vazia — sem membros, sem virtual, sem construtor não-trivial.
// PrefetchSingleton removido. Get() usa HeapAlloc para evitar guard do CRT.

class Prefetch {
public:
    static Prefetch* Get();          // implementado abaixo, após declaração completa
    int PrefetchSystem() { return PrefetchSystemWinAPI(); }
};

// Ponteiro POD — zero-initialized pelo VirtualAlloc, sem construtor necessário.
// Definido APÓS a classe para que 'Prefetch' seja um tipo completo.
static Prefetch* s_pf_ptr = NULL;

inline Prefetch* Prefetch::Get() {
    if (!s_pf_ptr)
        s_pf_ptr = (Prefetch*)HeapAlloc(GetProcessHeap(),
                                        HEAP_ZERO_MEMORY, sizeof(Prefetch));
    return s_pf_ptr;
}
