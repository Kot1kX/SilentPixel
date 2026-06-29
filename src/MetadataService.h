#pragma once
#include <windows.h>
#include <wincodec.h>
#include <string>
#include <vector>

struct MetadataEntry
{
    std::wstring section;
    std::wstring key;
    std::wstring value;
};

struct MetadataInfo
{
    std::wstring make;
    std::wstring model;
    std::wstring software;
    std::wstring dateOriginal;
    std::wstring exposureTime;
    std::wstring fNumber;
    std::wstring isoSpeed;
    std::wstring exposureMode;
    std::wstring focalLength;
    std::wstring focalLength35mm;
    std::wstring lensMake;
    std::wstring lensModel;
    std::wstring flash;
    std::wstring meteringMode;
    std::wstring whiteBalance;
    std::wstring digitalZoomRatio;
    std::wstring gpsLatitude;
    std::wstring gpsLongitude;
    std::wstring gpsRaw;
    std::wstring orientation;
    std::wstring pixelFormat;
    std::wstring colorProfile;
    std::wstring formatName;
    std::wstring containerNotes;
    std::wstring warning;
    UINT bitsPerPixel = 0;
    std::vector<MetadataEntry> entries;

    bool HasGps() const;
    bool HasShootingData() const;
    std::wstring GpsText() const;
    std::wstring ExifOrientationText(bool includeRawValue) const;
    std::wstring HumanSummary() const;
    std::wstring FullText() const;
};

class MetadataService
{
public:
    static MetadataInfo Extract(
        IWICBitmapDecoder* decoder,
        IWICBitmapFrameDecode* frame,
        const std::wstring& extension,
        const std::vector<BYTE>& rawBytes);

private:
    static std::wstring QueryString(IWICMetadataQueryReader* reader, LPCWSTR name);
    static std::wstring QueryAny(IWICMetadataQueryReader* reader, LPCWSTR name);
    static std::wstring PropVariantToString(const PROPVARIANT& pv);
    static std::wstring GuidToName(const GUID& guid);
    static void EnumerateReader(IWICMetadataQueryReader* reader, std::vector<MetadataEntry>& entries, const std::wstring& section, const std::wstring& prefix, int depth);
    static std::wstring RationalVectorToDecimal(const PROPVARIANT& pv);
    static std::wstring ExtensionToFormat(const std::wstring& extension);
    static UINT BitsPerPixelFromFormat(const GUID& guid);

    static void ScanRawContainer(const std::vector<BYTE>& rawBytes, const std::wstring& extension, MetadataInfo& info);
    static void ScanJpeg(const std::vector<BYTE>& rawBytes, MetadataInfo& info);
    static void ScanPng(const std::vector<BYTE>& rawBytes, MetadataInfo& info);
    static void ScanBmp(const std::vector<BYTE>& rawBytes, MetadataInfo& info);
    static std::wstring BytesToAsciiPreview(const BYTE* data, size_t size, size_t maxChars);
    static std::wstring ReadPngType(const BYTE* p);
    static UINT16 ReadBE16(const BYTE* p);
    static UINT32 ReadBE32(const BYTE* p);
    static UINT32 ReadLE32(const BYTE* p);
};
