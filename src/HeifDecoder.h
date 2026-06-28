#pragma once
#include "ImageLoader.h"
#include <string>
#include <vector>

class HeifDecoder
{
public:
    static bool LoadFromMemory(
        const std::wstring& path,
        const std::wstring& extension,
        ULONGLONG fileSize,
        FILETIME created,
        FILETIME modified,
        const std::vector<BYTE>& raw,
        LoadedImage& outImage,
        std::wstring& error);
};
