#pragma once
#include <string>
#include <cstdint>
#include <cstdio>

namespace SessionGuard {

    // Magic reconstructed from nibbles separados para evitar busca de padrão no binário
    static inline void _get_magic(uint8_t out[8]) {
        const uint8_t hi[8] = { 0xA0, 0x30, 0x90, 0x20, 0x50, 0x80, 0xD0, 0x60 };
        const uint8_t lo[8] = { 0x07, 0x0F, 0x0C, 0x0E, 0x0B, 0x01, 0x04, 0x0C };
        for (int i = 0; i < 8; i++) out[i] = hi[i] | lo[i];
        // Resultado: A7 3F 9C 2E 5B 81 D4 6C
    }

    // Retorna true APENAS se sessionCheck == XOR(sessionToken[0:8], MAGIC)
    // Ou seja: só passa se o servidor gerou o par — um atacante sem conta
    // não consegue computar um sessionCheck válido sem conhecer o MAGIC e o token.
    inline bool verify(const std::string& token, const std::string& check) {
        if (token.size() < 16 || check.size() < 16) return false;

        uint8_t magic[8];
        _get_magic(magic);

        for (int i = 0; i < 8; i++) {
            unsigned t = 0, c = 0;
            sscanf_s(token.c_str() + i * 2, "%02x", &t);
            sscanf_s(check.c_str()  + i * 2, "%02x", &c);
            if (((uint8_t)t ^ (uint8_t)c) != magic[i]) return false;
        }
        return true;
    }

}
