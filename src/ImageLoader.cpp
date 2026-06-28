#include "ImageLoader.h"
#include "HeifDecoder.h"
#include <shlwapi.h>
#include <algorithm>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")
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

    std::wstring PreviewFormatName(const std::wstring& extension)
    {
        const std::wstring ext = Lower(extension);
        if (ext == L".jpg" || ext == L".jpeg" || ext == L".jpe" || ext == L".jfif" || ext == L".jfi")
            return L"JPEG";
        if (ext == L".png")
            return L"PNG";
        if (ext == L".bmp")
            return L"BMP";
        if (ext == L".gif")
            return L"GIF";
        if (ext == L".tif" || ext == L".tiff")
            return L"TIFF";
        if (ext == L".ico")
            return L"ICO";
        if (ext == L".webp")
            return L"WEBP";
        if (ext == L".heic" || ext == L".heif")
            return L"HEIC/HEIF";
        return L"No disponible";
    }
}

ImageLoader::ImageLoader(IWICImagingFactory* factory) : factory_(factory)
{
}

bool ImageLoader::Load(const std::wstring& path, LoadedImage& outImage, std::wstring& error)
{
    return LoadInternal(path, outImage, error, false, 0);
}

bool ImageLoader::LoadPreview(const std::wstring& path, LoadedImage& outImage, std::wstring& error, UINT maxEdge)
{
    // HEIC_PREVIEW_BYPASS_TO_FULL_LOAD
    // HEIC/HEIF no debe pasar por la vista rapida WIC-only: en este equipo WIC no expone decoder.
    // Delegamos en Load(), que ya tiene fallback libheif.
    const wchar_t* previewExt = PathFindExtensionW(path.c_str());
    if (previewExt &&
        (_wcsicmp(previewExt, L".heic") == 0 || _wcsicmp(previewExt, L".heif") == 0))
    {
        return Load(path, outImage, error);
    }

    if (!factory_)
    {
        error = L"WIC no esta inicializado.";
        return false;
    }

    if (!IsSupportedExtension(path))
    {
        error = L"No se pudo abrir la imagen. El formato puede no estar soportado por los codecs instalados en Windows.";
        return false;
    }

    const std::wstring extension = ExtensionFromPath(path);

    WIN32_FILE_ATTRIBUTE_DATA attrs{};
    ULONGLONG fileSize = 0;
    FILETIME created{};
    FILETIME modified{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs))
    {
        fileSize = (static_cast<ULONGLONG>(attrs.nFileSizeHigh) << 32) | attrs.nFileSizeLow;
        created = attrs.ftCreationTime;
        modified = attrs.ftLastWriteTime;
    }

    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    UINT frameCount = 0;
    UINT frameIndex = 0;

    HRESULT hr = factory_->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder);

    if (SUCCEEDED(hr))
        decoder->GetFrameCount(&frameCount);

    if (SUCCEEDED(hr) && frameCount > 1 && extension == L".ico")
    {
        UINT64 bestArea = 0;
        for (UINT i = 0; i < frameCount; ++i)
        {
            IWICBitmapFrameDecode* candidate = nullptr;
            if (SUCCEEDED(decoder->GetFrame(i, &candidate)) && candidate)
            {
                UINT candidateWidth = 0;
                UINT candidateHeight = 0;
                if (SUCCEEDED(candidate->GetSize(&candidateWidth, &candidateHeight)))
                {
                    UINT64 area = static_cast<UINT64>(candidateWidth) * static_cast<UINT64>(candidateHeight);
                    if (area > bestArea)
                    {
                        bestArea = area;
                        frameIndex = i;
                    }
                }
                candidate->Release();
            }
        }
    }

    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(frameIndex, &frame);

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(hr))
        hr = frame->GetSize(&width, &height);

    if (FAILED(hr))
    {
        SafeRelease(frame);
        SafeRelease(decoder);
        error = CodecErrorForExtension(extension);
        return false;
    }

    const UINT maxDim = (width > height) ? width : height;

    UINT effectiveMaxEdge = maxEdge;
    if (fileSize >= 10ull * 1024ull * 1024ull || maxDim >= 6000)
        effectiveMaxEdge = 1600;

    if (effectiveMaxEdge == 0 || maxDim <= effectiveMaxEdge)
    {
        SafeRelease(frame);
        SafeRelease(decoder);
        return LoadInternal(path, outImage, error, false, 0);
    }

    UINT decodeWidth = static_cast<UINT>((static_cast<unsigned long long>(width) * effectiveMaxEdge + maxDim / 2) / maxDim);
    UINT decodeHeight = static_cast<UINT>((static_cast<unsigned long long>(height) * effectiveMaxEdge + maxDim / 2) / maxDim);
    if (decodeWidth < 1) decodeWidth = 1;
    if (decodeHeight < 1) decodeHeight = 1;

    hr = factory_->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr))
        hr = scaler->Initialize(frame, decodeWidth, decodeHeight, WICBitmapInterpolationModeLinear);

    if (SUCCEEDED(hr))
        hr = factory_->CreateFormatConverter(&converter);

    if (SUCCEEDED(hr))
    {
        hr = converter->Initialize(
            scaler,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }

    LoadedImage img;
    if (SUCCEEDED(hr))
    {
        img.width = decodeWidth;
        img.height = decodeHeight;
        img.originalWidth = width;
        img.originalHeight = height;
        img.preview = true;
        img.stride = decodeWidth * 4;
        img.pixelsBGRA.resize(static_cast<size_t>(img.stride) * img.height);
        hr = converter->CopyPixels(nullptr, img.stride, static_cast<UINT>(img.pixelsBGRA.size()), img.pixelsBGRA.data());
    }

    if (FAILED(hr))
    {
        SafeRelease(converter);
        SafeRelease(scaler);
        SafeRelease(frame);
        SafeRelease(decoder);
        error = CodecErrorForExtension(extension);
        return false;
    }

    img.path = path;
    img.fileName = FileNameFromPath(path);
    img.extension = extension;
    img.fileSize = fileSize;
    img.created = created;
    img.modified = modified;

    // Modo ligero, pero con lectura rÃ¡pida de metadatos desde WIC.
    // No decodifica la imagen completa ni calcula hashes, pero ya permite ver Make/Model/ISO/focal/etc.
    {
        // SilentPixel_PREVIEW_RAW_SNIFF_PARTIAL_2MB
        // Modo ligero: lee solo una cabecera parcial para detectar EXIF/XMP/ICC/IPTC
        // sin calcular hashes ni cargar el archivo completo en memoria.
        std::vector<BYTE> rawPreview;

        std::ifstream rawStream(path, std::ios::binary);
        if (rawStream)
        {
            constexpr std::streamsize maxRawPreviewBytes = 2 * 1024 * 1024;
            rawPreview.resize(static_cast<size_t>(maxRawPreviewBytes));
            rawStream.read(reinterpret_cast<char*>(rawPreview.data()), maxRawPreviewBytes);

            const std::streamsize got = rawStream.gcount();
            if (got > 0)
                rawPreview.resize(static_cast<size_t>(got));
            else
                rawPreview.clear();
        }

        img.metadata = MetadataService::Extract(decoder, frame, extension, rawPreview);

        if (!rawPreview.empty())
            SecureZeroMemory(rawPreview.data(), rawPreview.size());
        std::vector<BYTE>().swap(rawPreview);
    }

    if (img.metadata.formatName.empty())
        img.metadata.formatName = PreviewFormatName(extension);

    img.metadata.pixelFormat = L"Modo ligero 32bpp PBGRA";
    if (img.metadata.bitsPerPixel == 0)
        img.metadata.bitsPerPixel = 32;

    if (!img.metadata.warning.empty())
        img.metadata.warning += L" ";
    img.metadata.warning += L"Modo ligero activo para navegar fluido. Hashes y contenedor bruto completo al exportar/copiar.";

    if (img.metadata.containerNotes.empty())
        img.metadata.containerNotes = L"Modo ligero: contenedor completo pendiente hasta exportar/copiar.";

    img.hashes.sha256 = L"Pendiente";
    img.hashes.sha1 = L"Pendiente";
    img.hashes.md5 = L"Pendiente";

    SafeRelease(converter);
    SafeRelease(scaler);
    SafeRelease(frame);
    SafeRelease(decoder);

    outImage = std::move(img);
    return true;
}

bool ImageLoader::LoadInternal(const std::wstring& path, LoadedImage& outImage, std::wstring& error, bool preview, UINT maxEdge)
{
    if (!factory_)
    {
        error = L"WIC no esta inicializado.";
        return false;
    }

    if (!IsSupportedExtension(path))
    {
        error = L"No se pudo abrir la imagen. El formato puede no estar soportado por los codecs instalados en Windows.";
        return false;
    }

    const std::wstring initialExt = ExtensionFromPath(path);

    std::vector<BYTE> raw;
    ULONGLONG fileSize = 0;
    FILETIME created{};
    FILETIME modified{};
    if (!ReadWholeFile(path, raw, fileSize, created, modified, error))
        return false;

    if (raw.empty() || raw.size() > static_cast<size_t>(0xFFFFFFFFu))
    {
        error = L"Archivo vacio o demasiado grande para el flujo WIC en memoria.";
        return false;
    }

    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    UINT frameCount = 0;
    UINT frameIndex = 0;
    std::wstring sourceExt = ExtensionFromPath(path);

    HRESULT hr = factory_->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromMemory(raw.data(), static_cast<DWORD>(raw.size()));
    if (SUCCEEDED(hr))
        hr = factory_->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);

    const bool isHeifLike = (initialExt == L".heic" || initialExt == L".heif");

    // Algunos codecs HEIF/HEIC de Windows se comportan mejor decodificando desde filename
    // que desde IWICStream en memoria. Primero se intenta memoria; si falla,
    // se usa fallback de archivo solo para HEIC/HEIF y se libera inmediatamente al terminar.
    if (FAILED(hr) && isHeifLike)
    {
        SafeRelease(decoder);
        SafeRelease(stream);

        hr = factory_->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,
            &decoder);
    }

    if (SUCCEEDED(hr))
        decoder->GetFrameCount(&frameCount);
    if (SUCCEEDED(hr) && frameCount > 1 && sourceExt == L".ico")
    {
        UINT64 bestArea = 0;
        for (UINT i = 0; i < frameCount; ++i)
        {
            IWICBitmapFrameDecode* candidate = nullptr;
            if (SUCCEEDED(decoder->GetFrame(i, &candidate)) && candidate)
            {
                UINT candidateWidth = 0;
                UINT candidateHeight = 0;
                if (SUCCEEDED(candidate->GetSize(&candidateWidth, &candidateHeight)))
                {
                    UINT64 area = static_cast<UINT64>(candidateWidth) * static_cast<UINT64>(candidateHeight);
                    if (area > bestArea)
                    {
                        bestArea = area;
                        frameIndex = i;
                    }
                }
                candidate->Release();
            }
        }
    }
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(frameIndex, &frame);

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(hr))
        hr = frame->GetSize(&width, &height);

    UINT decodeWidth = width;
    UINT decodeHeight = height;
    bool usingPreview = false;
    IWICBitmapSource* source = frame;

    if (SUCCEEDED(hr) && preview && maxEdge > 0 && width > 0 && height > 0)
    {
        const UINT maxDim = (width > height) ? width : height;
        if (maxDim > maxEdge)
        {
            decodeWidth = static_cast<UINT>((static_cast<unsigned long long>(width) * maxEdge + maxDim / 2) / maxDim);
            decodeHeight = static_cast<UINT>((static_cast<unsigned long long>(height) * maxEdge + maxDim / 2) / maxDim);
            if (decodeWidth < 1) decodeWidth = 1;
            if (decodeHeight < 1) decodeHeight = 1;

            hr = factory_->CreateBitmapScaler(&scaler);
            if (SUCCEEDED(hr))
                hr = scaler->Initialize(frame, decodeWidth, decodeHeight, WICBitmapInterpolationModeFant);
            if (SUCCEEDED(hr))
            {
                source = scaler;
                usingPreview = true;
            }
        }
    }

    if (SUCCEEDED(hr))
        hr = factory_->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
    {
        hr = converter->Initialize(
            source,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }

    LoadedImage img;
    if (SUCCEEDED(hr))
    {
        img.width = decodeWidth;
        img.height = decodeHeight;
        img.originalWidth = width;
        img.originalHeight = height;
        img.preview = usingPreview;
        img.stride = decodeWidth * 4;
        img.pixelsBGRA.resize(static_cast<size_t>(img.stride) * img.height);
        hr = converter->CopyPixels(nullptr, img.stride, static_cast<UINT>(img.pixelsBGRA.size()), img.pixelsBGRA.data());
    }

    if (FAILED(hr))
    {
        SafeRelease(converter);
        SafeRelease(scaler);
        SafeRelease(frame);
        SafeRelease(decoder);
        SafeRelease(stream);

        if (initialExt == L".heic" || initialExt == L".heif")
        {
            LoadedImage heifImage;
            std::wstring heifError;
            if (HeifDecoder::LoadFromMemory(path, initialExt, fileSize, created, modified, raw, heifImage, heifError))
            {
                if (!raw.empty())
                    SecureZeroMemory(raw.data(), raw.size());
                std::vector<BYTE>().swap(raw);
                outImage = std::move(heifImage);
                return true;
            }

            error = heifError.empty() ? CodecErrorForExtension(initialExt) : heifError;
        }
        else
        {
            error = CodecErrorForExtension(initialExt);
        }

        if (!raw.empty())
            SecureZeroMemory(raw.data(), raw.size());
        return false;
    }

    img.path = path;
    img.fileName = FileNameFromPath(path);
    img.extension = ExtensionFromPath(path);
    img.fileSize = fileSize;
    img.created = created;
    img.modified = modified;
    img.metadata = MetadataService::Extract(decoder, frame, img.extension, raw);
    if (frameCount > 1 && (img.extension == L".tif" || img.extension == L".tiff"))
    {
        if (!img.metadata.warning.empty())
            img.metadata.warning += L" ";
        img.metadata.warning += L"TIFF multip\u00E1gina: SilentPixel ha cargado solo la primera p\u00E1gina.";
    }
    else if (frameCount > 1 && img.extension == L".gif")
    {
        if (!img.metadata.warning.empty())
            img.metadata.warning += L" ";
        img.metadata.warning += L"GIF con varios frames: SilentPixel ha cargado solo el primer frame.";
    }
    img.hashes = HashService::ComputeAll(raw);

    SafeRelease(converter);
    SafeRelease(scaler);
    SafeRelease(frame);
    SafeRelease(decoder);
    SafeRelease(stream);

    SecureZeroMemory(raw.data(), raw.size());
    std::vector<BYTE>().swap(raw);

    outImage = std::move(img);
    return true;
}

bool ImageLoader::ReadWholeFile(const std::wstring& path, std::vector<BYTE>& data, ULONGLONG& fileSize, FILETIME& created, FILETIME& modified, std::wstring& error)
{
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        error = L"No se pudo abrir el archivo.";
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0)
    {
        CloseHandle(file);
        error = L"No se pudo leer el tamano del archivo.";
        return false;
    }

    if (size.QuadPart > 512ll * 1024ll * 1024ll)
    {
        CloseHandle(file);
        error = L"Archivo demasiado grande para esta build.";
        return false;
    }

    fileSize = static_cast<ULONGLONG>(size.QuadPart);
    GetFileTime(file, &created, nullptr, &modified);
    data.resize(static_cast<size_t>(size.QuadPart));

    BYTE* dst = data.data();
    size_t remaining = data.size();
    while (remaining > 0)
    {
        DWORD chunk = static_cast<DWORD>(remaining > (4 * 1024 * 1024) ? (4 * 1024 * 1024) : remaining);
        DWORD read = 0;
        if (!ReadFile(file, dst, chunk, &read, nullptr) || read == 0)
        {
            CloseHandle(file);
            error = L"Error leyendo el archivo completo.";
            return false;
        }
        dst += read;
        remaining -= read;
    }

    CloseHandle(file);
    return true;
}

bool ImageLoader::IsSupportedExtension(const std::wstring& path)
{
    std::wstring ext = Lower(ExtensionFromPath(path));
    return ext == L".jpg"  ||
           ext == L".jpeg" ||
           ext == L".jpe"  ||
           ext == L".jfif" ||
           ext == L".jfi"  ||
           ext == L".png"  ||
           ext == L".bmp"  ||
           ext == L".gif"  ||
           ext == L".tif"  ||
           ext == L".tiff" ||
           ext == L".ico"  ||
           ext == L".webp" ||
           ext == L".heic" ||
           ext == L".heif";
}

std::wstring ImageLoader::CodecErrorForExtension(const std::wstring& extension)
{
    const std::wstring ext = Lower(extension);

    if (ext == L".heic" || ext == L".heif")
    {
        return L"No se pudo abrir HEIC/HEIF. Windows no expone un decoder WIC compatible para este archivo. Instala el codec HEIF/HEVC del sistema o convierte la imagen a JPEG/PNG.";
    }

    if (ext == L".webp")
    {
        return L"No se pudo abrir WEBP. Windows no expone un decoder WIC compatible para este archivo.";
    }

    return L"No se pudo abrir la imagen. El formato puede no estar soportado por los codecs instalados en Windows.";
}

std::wstring ImageLoader::FileNameFromPath(const std::wstring& path)
{
    const wchar_t* name = PathFindFileNameW(path.c_str());
    return name ? name : path;
}

std::wstring ImageLoader::ExtensionFromPath(const std::wstring& path)
{
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    return ext ? Lower(ext) : L"";
}







