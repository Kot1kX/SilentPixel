#pragma once
#include <windows.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include "MetadataService.h"
#include "HashService.h"

struct LoadedImage
{
    std::wstring path;
    std::wstring fileName;
    std::wstring extension;
    UINT width = 0;
    UINT height = 0;
    UINT originalWidth = 0;
    UINT originalHeight = 0;
    UINT stride = 0;
    bool preview = false;
    std::vector<BYTE> pixelsBGRA;
    ULONGLONG fileSize = 0;
    FILETIME created{};
    FILETIME modified{};
    MetadataInfo metadata;
    HashResult hashes;
};

class ImageLoader
{
public:
    explicit ImageLoader(IWICImagingFactory* factory);
    bool Load(const std::wstring& path, LoadedImage& outImage, std::wstring& error);
    bool LoadPreview(const std::wstring& path, LoadedImage& outImage, std::wstring& error, UINT maxEdge = 2560);
    static bool IsSupportedExtension(const std::wstring& path);
    static std::wstring FileNameFromPath(const std::wstring& path);
    static std::wstring ExtensionFromPath(const std::wstring& path);

private:
    IWICImagingFactory* factory_ = nullptr;
    bool LoadInternal(const std::wstring& path, LoadedImage& outImage, std::wstring& error, bool preview, UINT maxEdge);
    static bool ReadWholeFile(const std::wstring& path, std::vector<BYTE>& data, ULONGLONG& fileSize, FILETIME& created, FILETIME& modified, std::wstring& error);
    static std::wstring CodecErrorForExtension(const std::wstring& extension);
};
