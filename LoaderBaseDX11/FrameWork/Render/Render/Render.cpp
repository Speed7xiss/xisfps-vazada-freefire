#include "Render.hpp"
#include <string>

struct WindowSearchData {
    const char* targetClassName;
    HWND foundWindow;
};

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam)
{
    WindowSearchData* searchData = reinterpret_cast<WindowSearchData*>(lParam);
    char className[256];
    if (RealGetWindowClassA(hwnd, className, sizeof(className)) > 0) {
        const char* a = searchData->targetClassName;
        const char* b = className;

        while (*a && *b && *a == *b) {
            a++;
            b++;
        }

        if (*a == '\0' && *b == '\0') {
            searchData->foundWindow = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL CALLBACK EnumChildWindowsCallback(HWND hwnd, LPARAM lParam)
{
    return EnumWindowsCallback(hwnd, lParam);
}

static BOOL CALLBACK EnumWindowsForChildSearch(HWND hwnd, LPARAM lParam)
{
    WindowSearchData* searchData = reinterpret_cast<WindowSearchData*>(lParam);
    EnumChildWindows(hwnd, EnumChildWindowsCallback, lParam);
    return (searchData->foundWindow == NULL);
}

static HWND FindByClassName(const char* className) {
    WindowSearchData searchData;
    searchData.targetClassName = className;
    searchData.foundWindow = NULL;

    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&searchData));

    if (searchData.foundWindow == NULL) {
        EnumWindows(EnumWindowsForChildSearch, reinterpret_cast<LPARAM>(&searchData));
    }

    return searchData.foundWindow;
}

struct TitleSearchData {
    HWND foundWindow;
};

static BOOL CALLBACK EnumByTitleCallback(HWND hwnd, LPARAM lParam)
{
    TitleSearchData* data = reinterpret_cast<TitleSearchData*>(lParam);
    char title[256];
    if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
        if (strstr(title, "BlueStacks") != nullptr || strstr(title, "bluestacks") != nullptr) {
            if (IsWindowVisible(hwnd)) {
                data->foundWindow = hwnd;
                return FALSE;
            }
        }
    }
    return TRUE;
}

HWND Render::LookupWindowByClassName(const char* toFindClassName)
{
    if (toFindClassName) {
        HWND result = FindByClassName(toFindClassName);
        if (result) return result;
    }

    const char* classNames[] = {
        "BlueStacksApp",
        "Qt5154QWindowOwnDCIcon",
        "Qt5154QWindowIcon",
        "Qt515QWindowOwnDCIcon",
        "Qt515QWindowIcon",
        "HwndWrapper[BlueStacks*",
        "pltte",
    };

    for (auto& cn : classNames) {
        HWND result = FindByClassName(cn);
        if (result) return result;
    }

    TitleSearchData tsd;
    tsd.foundWindow = NULL;
    EnumWindows(EnumByTitleCallback, reinterpret_cast<LPARAM>(&tsd));
    if (tsd.foundWindow) return tsd.foundWindow;

    return NULL;
}
