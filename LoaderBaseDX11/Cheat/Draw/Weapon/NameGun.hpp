#pragma once
#include <string>
#include <vector>

class Namegun {
public:
    struct GunInfo {
        std::string name;
        std::string icon;      // UTF-8 icon glyph (e.g. "") — empty if none
        bool        isSpecial = false;
    };

    static void        Init();
    // Return const char* backed by the static GunData table — zero heap allocation
    // per call. GunData is filled once in Init() and never mutated after, so the
    // .c_str() pointers are stable for the program lifetime. Empty result is an
    // empty C string (str[0] == '\0'), matching the old std::string().empty() semantics.
    static const char* GetGunName(short gunId);
    static const char* GetGunIcon(short gunId);
    static bool        HasIcon(short gunId);

private:
    static std::vector<GunInfo> GunData;
};
