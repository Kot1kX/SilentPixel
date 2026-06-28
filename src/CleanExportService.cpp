#include "CleanExportService.h"
#include <algorithm>
#include <sstream>

#pragma comment(lib, "windowscodecs.lib")

namespace
{
    template <typename T>
    void SafeRelease(T*& p)
    {
        if (p)
        {
            p->Release();
            p = nullptr;
        }
    }

    std::wstring Lower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return value;
    }
}

CleanExportService::CleanExportService(IWICImagingFactory* factory) : factory_(factory)
{
}

bool CleanExportService::SaveCleanCopy(const LoadedImage& image, const std::wstring& destination, std::wstring& error)
{
    if (!factory_ || image.pixelsBGRA.empty())
    {
        error = L"No hay imagen cargada.";
        return false;
    }

    IWICBitmap* bitmap = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* bag = nullptr;

    HRESULT hr = factory_->CreateBitmapFromMemory(
        image.width,
        image.height,
        GUID_WICPixelFormat32bppPBGRA,
        image.stride,
        static_cast<UINT>(image.pixelsBGRA.size()),
        const_cast<BYTE*>(image.pixelsBGRA.data()),
        &bitmap);

    if (SUCCEEDED(hr))
        hr = factory_->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromFilename(destination.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr))
        hr = factory_->CreateEncoder(ContainerFromExtension(destination), nullptr, &encoder);
    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&frame, &bag);

    if (SUCCEEDED(hr))
    {
        GUID container = ContainerFromExtension(destination);
        if (container == GUID_ContainerFormatJpeg && bag)
        {
            PROPBAG2 option{};
            option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
            VARIANT value{};
            VariantInit(&value);
            value.vt = VT_R4;
            value.fltVal = 0.95f;
            bag->Write(1, &option, &value);
            VariantClear(&value);
        }

        hr = frame->Initialize(bag);
    }

    if (SUCCEEDED(hr)) hr = frame->SetSize(image.width, image.height);

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(bitmap, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();

    SafeRelease(bag);
    SafeRelease(frame);
    SafeRelease(encoder);
    SafeRelease(stream);
    SafeRelease(bitmap);

    if (FAILED(hr))
    {
        std::wstringstream ss;
        ss << L"No se pudo guardar la copia limpia. HRESULT=0x" << std::hex << hr;
        error = ss.str();
        return false;
    }

    return true;
}

GUID CleanExportService::ContainerFromExtension(const std::wstring& path)
{
    std::wstring lower = Lower(path);
    if ((lower.size() >= 4 && (lower.rfind(L".jpg") == lower.size() - 4 || lower.rfind(L".jpe") == lower.size() - 4)) ||
        (lower.size() >= 5 && (lower.rfind(L".jpeg") == lower.size() - 5 || lower.rfind(L".jfif") == lower.size() - 5)) ||
        (lower.size() >= 4 && lower.rfind(L".jfi") == lower.size() - 4))
        return GUID_ContainerFormatJpeg;
    if (lower.size() >= 4 && lower.rfind(L".bmp") == lower.size() - 4)
        return GUID_ContainerFormatBmp;
    return GUID_ContainerFormatPng;
}
