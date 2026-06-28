#pragma once
#include <windows.h>
#include <wincodec.h>
#include <string>
#include "ImageLoader.h"

class CleanExportService
{
public:
    explicit CleanExportService(IWICImagingFactory* factory);
    bool SaveCleanCopy(const LoadedImage& image, const std::wstring& destination, std::wstring& error);

private:
    IWICImagingFactory* factory_ = nullptr;
    static GUID ContainerFromExtension(const std::wstring& path);
};
