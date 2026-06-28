#pragma once
#include <windows.h>
#include <string>
#include <vector>

class PrivacyGuard
{
public:
    static void ZeroVector(std::vector<BYTE>& data);
    static void CopyTextToClipboard(HWND owner, const std::wstring& text);
    static void OpenContainingFolder(const std::wstring& path);
    static std::wstring ExplorerSelectCommand(const std::wstring& path);
};
