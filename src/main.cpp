#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "MainWindow.h"
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

static void EnableBasicDpiAwareness()
{
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (!user32)
        return;

    using SetDpiContextFn = BOOL(WINAPI*)(HANDLE);
    auto setContext = reinterpret_cast<SetDpiContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext)
    {
        setContext(reinterpret_cast<HANDLE>(-4));
        FreeLibrary(user32);
        return;
    }

    using SetDpiAwareFn = BOOL(WINAPI*)();
    auto setAware = reinterpret_cast<SetDpiAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"));
    if (setAware)
        setAware();

    FreeLibrary(user32);
}


static std::wstring FirstCommandLinePath()
{
    std::wstring result;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2 && argv[1] && argv[1][0] != L'\0')
        result = argv[1];
    if (argv)
        LocalFree(argv);

    if (!result.empty())
    {
        std::vector<wchar_t> fullPath(32768, L'\0');
        DWORD length = GetFullPathNameW(result.c_str(), static_cast<DWORD>(fullPath.size()), fullPath.data(), nullptr);
        if (length > 0 && length < fullPath.size())
            result = fullPath.data();
    }

    return result;
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCmd)
{
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    EnableBasicDpiAwareness();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(com);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
    {
        MessageBoxW(nullptr, L"No se pudo iniciar COM.", L"SilentPixel", MB_ICONERROR);
        return 1;
    }

    MainWindow window;
    std::wstring startupPath = FirstCommandLinePath();
    if (!window.Create(instance, showCmd, startupPath))
    {
        if (comInitialized)
            CoUninitialize();
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (comInitialized)
        CoUninitialize();

    if (gdiplusToken)
        Gdiplus::GdiplusShutdown(gdiplusToken);

    return static_cast<int>(msg.wParam);
}


