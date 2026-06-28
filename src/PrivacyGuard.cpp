#include "PrivacyGuard.h"
#include <shellapi.h>
#include <algorithm>

void PrivacyGuard::ZeroVector(std::vector<BYTE>& data)
{
    if (!data.empty())
    {
        SecureZeroMemory(data.data(), data.size());
        std::vector<BYTE>().swap(data);
    }
}

void PrivacyGuard::CopyTextToClipboard(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner))
        return;

    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem)
    {
        void* ptr = GlobalLock(mem);
        if (ptr)
        {
            CopyMemory(ptr, text.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
            mem = nullptr;
        }
    }
    if (mem)
        GlobalFree(mem);
    CloseClipboard();
}

std::wstring PrivacyGuard::ExplorerSelectCommand(const std::wstring& path)
{
    return L"/select,\"" + path + L"\"";
}

void PrivacyGuard::OpenContainingFolder(const std::wstring& path)
{
    std::wstring args = ExplorerSelectCommand(path);
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}
