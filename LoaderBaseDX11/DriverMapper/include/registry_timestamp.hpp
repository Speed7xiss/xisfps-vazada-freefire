#include "nt.hpp"
#include <vector>
#include <string>

namespace registry_timestamp {

    struct SavedTimestamp {
        std::wstring keyPath;
        LARGE_INTEGER lastWriteTime;
    };

    static std::vector<SavedTimestamp> savedTimestamps;

    bool SaveParentKeyTimestamp() {
        savedTimestamps.clear();

        const std::wstring paths[] = {
            L"SYSTEM\\CurrentControlSet\\Services",
            L"SYSTEM\\ControlSet001\\Services",
            L"SYSTEM\\ControlSet002\\Services"
        };

        for (const auto& path : paths) {
            HKEY hKey = NULL;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
                continue;

            FILETIME ft = { 0 };
            LONG status = RegQueryInfoKeyW(
                hKey, NULL, NULL, NULL, NULL, NULL, NULL,
                NULL, NULL, NULL, NULL, &ft
            );
            RegCloseKey(hKey);

            if (status != ERROR_SUCCESS)
                continue;

            SavedTimestamp ts;
            ts.keyPath = path;
            ts.lastWriteTime.LowPart = ft.dwLowDateTime;
            ts.lastWriteTime.HighPart = ft.dwHighDateTime;
            savedTimestamps.push_back(ts);
        }

        return !savedTimestamps.empty();
    }

    bool RestoreParentKeyTimestamp() {
        if (savedTimestamps.empty()) {
            return false;
        }

        static nt::t_NtSetInformationKey pNtSetInformationKey = nullptr;
        if (!pNtSetInformationKey) {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (!ntdll) return false;
            pNtSetInformationKey = (nt::t_NtSetInformationKey)GetProcAddress(ntdll, "NtSetInformationKey");
            if (!pNtSetInformationKey) {
                return false;
            }
        }

        bool allOk = true;
        for (const auto& ts : savedTimestamps) {
            HKEY hKey = NULL;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ts.keyPath.c_str(), 0,
                KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
                allOk = false;
                continue;
            }

            nt::KEY_WRITE_TIME_INFORMATION writeTimeInfo;
            writeTimeInfo.LastWriteTime = ts.lastWriteTime;

            NTSTATUS ntStatus = pNtSetInformationKey(
                hKey,
                nt::KeyWriteTimeInformation,
                &writeTimeInfo,
                sizeof(nt::KEY_WRITE_TIME_INFORMATION)
            );
            RegCloseKey(hKey);
        }

        savedTimestamps.clear();
        return allOk;
    }

}