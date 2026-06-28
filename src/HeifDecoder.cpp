#include "HeifDecoder.h"
#include "HashService.h"

#include <libheif/heif.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

namespace
{
    std::wstring FileNameFromPathLocal(const std::wstring& path)
    {
        const wchar_t* name = PathFindFileNameW(path.c_str());
        return name ? name : path;
    }

    std::wstring Utf8ToWide(const char* text)
    {
        if (!text || !*text)
            return L"";

        int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (len <= 0)
            return L"";

        std::wstring out(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), len);
        while (!out.empty() && out.back() == L'\0')
            out.pop_back();
        return out;
    }

    std::wstring CleanLine(std::wstring s)
    {
        for (wchar_t& c : s)
        {
            if (c == L'\r' || c == L'\n' || c == L'\t')
                c = L' ';
        }
        while (!s.empty() && s.front() == L' ') s.erase(s.begin());
        while (!s.empty() && s.back() == L' ') s.pop_back();
        return s;
    }

    std::wstring TrimNumber(std::wstring s)
    {
        const size_t dot = s.find(L'.');
        if (dot != std::wstring::npos)
        {
            while (!s.empty() && s.back() == L'0') s.pop_back();
            if (!s.empty() && s.back() == L'.') s.pop_back();
        }
        if (s == L"-0") s = L"0";
        return s;
    }

    std::wstring FormatDouble(double value, int precision)
    {
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(precision) << value;
        return TrimNumber(ss.str());
    }

    std::wstring FormatExposureTime(double seconds)
    {
        if (seconds <= 0.0)
            return L"";

        if (seconds < 0.5)
        {
            const int denom = static_cast<int>(std::llround(1.0 / seconds));
            if (denom > 0)
                return L"1/" + std::to_wstring(denom) + L" s";
        }

        return FormatDouble(seconds, 3) + L" s";
    }

    std::wstring FormatFNumber(double value)
    {
        if (value <= 0.0)
            return L"";
        return L"f/" + FormatDouble(value, 2);
    }

    std::wstring FormatFocalLength(double value)
    {
        if (value <= 0.0)
            return L"";
        return FormatDouble(value, 1) + L" mm";
    }

    std::wstring ExposureModeName(int value)
    {
        if (value == 0) return L"Auto";
        if (value == 1) return L"Manual";
        if (value == 2) return L"Auto bracket";
        return std::to_wstring(value);
    }

    std::wstring MeteringModeName(int value)
    {
        if (value == 1) return L"Media";
        if (value == 2) return L"Ponderada al centro";
        if (value == 3) return L"Puntual";
        if (value == 4) return L"Multipunto";
        if (value == 5) return L"Patron/matriz";
        if (value == 6) return L"Parcial";
        if (value == 255) return L"Otro";
        return std::to_wstring(value);
    }

    std::wstring WhiteBalanceName(int value)
    {
        if (value == 0) return L"Auto";
        if (value == 1) return L"Manual";
        return std::to_wstring(value);
    }

    std::wstring FlashText(int raw)
    {
        return (raw & 1) ? L"Disparado" : L"No disparado";
    }

    std::wstring HeifErrorText(const heif_error& err, const wchar_t* context)
    {
        std::wstringstream ss;
        ss << context;
        if (err.message && *err.message)
            ss << L": " << Utf8ToWide(err.message);
        ss << L" (code=" << err.code << L", subcode=" << err.subcode << L")";
        return ss.str();
    }

    bool Ok(const heif_error& err)
    {
        return err.code == heif_error_Ok;
    }

    struct ExifReader
    {
        const uint8_t* data = nullptr;
        size_t size = 0;
        size_t tiff = 0;
        bool le = true;
        bool valid = false;

        explicit ExifReader(const std::vector<uint8_t>& payload)
        {
            data = payload.data();
            size = payload.size();
            valid = FindTiffHeader();
        }

        bool FindTiffHeader()
        {
            if (!data || size < 8)
                return false;

            const size_t scanMax = (std::min)(size - 4, static_cast<size_t>(128));
            for (size_t i = 0; i <= scanMax; ++i)
            {
                if (data[i] == 'I' && data[i + 1] == 'I' && data[i + 2] == 0x2A && data[i + 3] == 0x00)
                {
                    tiff = i;
                    le = true;
                    return true;
                }

                if (data[i] == 'M' && data[i + 1] == 'M' && data[i + 2] == 0x00 && data[i + 3] == 0x2A)
                {
                    tiff = i;
                    le = false;
                    return true;
                }
            }

            return false;
        }

        bool Read16Abs(size_t pos, uint16_t& out) const
        {
            if (pos + 2 > size)
                return false;

            if (le)
                out = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
            else
                out = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
            return true;
        }

        bool Read32Abs(size_t pos, uint32_t& out) const
        {
            if (pos + 4 > size)
                return false;

            if (le)
            {
                out = static_cast<uint32_t>(data[pos]) |
                      (static_cast<uint32_t>(data[pos + 1]) << 8) |
                      (static_cast<uint32_t>(data[pos + 2]) << 16) |
                      (static_cast<uint32_t>(data[pos + 3]) << 24);
            }
            else
            {
                out = (static_cast<uint32_t>(data[pos]) << 24) |
                      (static_cast<uint32_t>(data[pos + 1]) << 16) |
                      (static_cast<uint32_t>(data[pos + 2]) << 8) |
                       static_cast<uint32_t>(data[pos + 3]);
            }

            return true;
        }

        static uint32_t TypeSize(uint16_t type)
        {
            switch (type)
            {
            case 1: return 1; // BYTE
            case 2: return 1; // ASCII
            case 3: return 2; // SHORT
            case 4: return 4; // LONG
            case 5: return 8; // RATIONAL
            case 7: return 1; // UNDEFINED
            case 9: return 4; // SLONG
            case 10: return 8; // SRATIONAL
            default: return 0;
            }
        }

        bool EntryValuePos(size_t entry, uint16_t type, uint32_t count, size_t& pos, size_t& bytes) const
        {
            const uint32_t unit = TypeSize(type);
            if (unit == 0)
                return false;

            const uint64_t total = static_cast<uint64_t>(unit) * static_cast<uint64_t>(count);
            if (total > static_cast<uint64_t>(SIZE_MAX))
                return false;

            bytes = static_cast<size_t>(total);

            if (bytes <= 4)
            {
                pos = entry + 8;
                return pos + bytes <= size;
            }

            uint32_t rel = 0;
            if (!Read32Abs(entry + 8, rel))
                return false;

            pos = tiff + static_cast<size_t>(rel);
            return pos + bytes <= size;
        }

        bool IfdEntries(uint32_t relOffset, std::vector<size_t>& entries) const
        {
            entries.clear();

            const size_t ifd = tiff + static_cast<size_t>(relOffset);
            uint16_t count = 0;
            if (!Read16Abs(ifd, count))
                return false;

            size_t entry = ifd + 2;
            for (uint16_t i = 0; i < count; ++i)
            {
                if (entry + 12 > size)
                    return false;

                entries.push_back(entry);
                entry += 12;
            }

            return true;
        }

        bool EntryHeader(size_t entry, uint16_t& tag, uint16_t& type, uint32_t& count) const
        {
            return Read16Abs(entry, tag) &&
                   Read16Abs(entry + 2, type) &&
                   Read32Abs(entry + 4, count);
        }

        bool ReadLongValue(size_t entry, uint32_t& out) const
        {
            uint16_t tag = 0, type = 0;
            uint32_t count = 0;
            if (!EntryHeader(entry, tag, type, count) || count < 1)
                return false;

            size_t pos = 0, bytes = 0;
            if (!EntryValuePos(entry, type, count, pos, bytes))
                return false;

            if (type == 3)
            {
                uint16_t v = 0;
                if (!Read16Abs(pos, v))
                    return false;
                out = v;
                return true;
            }

            if (type == 4)
                return Read32Abs(pos, out);

            return false;
        }

        std::wstring ReadAscii(size_t entry) const
        {
            uint16_t tag = 0, type = 0;
            uint32_t count = 0;
            if (!EntryHeader(entry, tag, type, count) || type != 2 || count == 0)
                return L"";

            size_t pos = 0, bytes = 0;
            if (!EntryValuePos(entry, type, count, pos, bytes) || bytes == 0)
                return L"";

            std::string raw;
            raw.reserve(bytes);
            for (size_t i = 0; i < bytes && pos + i < size; ++i)
            {
                char c = static_cast<char>(data[pos + i]);
                if (c == '\0')
                    break;
                raw.push_back(c);
            }

            if (raw.empty())
                return L"";

            int len = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), nullptr, 0);
            UINT cp = CP_UTF8;
            if (len <= 0)
            {
                cp = CP_ACP;
                len = MultiByteToWideChar(CP_ACP, 0, raw.c_str(), static_cast<int>(raw.size()), nullptr, 0);
            }

            if (len <= 0)
                return L"";

            std::wstring out(static_cast<size_t>(len), L'\0');
            MultiByteToWideChar(cp, 0, raw.c_str(), static_cast<int>(raw.size()), out.data(), len);
            return CleanLine(out);
        }

        bool ReadNumber(size_t entry, int& out) const
        {
            uint32_t v = 0;
            if (!ReadLongValue(entry, v))
                return false;
            out = static_cast<int>(v);
            return true;
        }

        bool ReadRational(size_t entry, double& out) const
        {
            uint16_t tag = 0, type = 0;
            uint32_t count = 0;
            if (!EntryHeader(entry, tag, type, count) || count < 1)
                return false;

            if (type != 5 && type != 10)
                return false;

            size_t pos = 0, bytes = 0;
            if (!EntryValuePos(entry, type, count, pos, bytes) || pos + 8 > size)
                return false;

            uint32_t n = 0, d = 0;
            if (!Read32Abs(pos, n) || !Read32Abs(pos + 4, d) || d == 0)
                return false;

            out = static_cast<double>(n) / static_cast<double>(d);
            return true;
        }

        bool ReadGpsTriple(size_t entry, double& degrees, double& minutes, double& seconds) const
        {
            uint16_t tag = 0, type = 0;
            uint32_t count = 0;
            if (!EntryHeader(entry, tag, type, count) || type != 5 || count < 3)
                return false;

            size_t pos = 0, bytes = 0;
            if (!EntryValuePos(entry, type, count, pos, bytes) || pos + 24 > size)
                return false;

            auto readRat = [&](size_t p, double& out) -> bool
            {
                uint32_t n = 0, d = 0;
                if (!Read32Abs(p, n) || !Read32Abs(p + 4, d) || d == 0)
                    return false;
                out = static_cast<double>(n) / static_cast<double>(d);
                return true;
            };

            return readRat(pos, degrees) &&
                   readRat(pos + 8, minutes) &&
                   readRat(pos + 16, seconds);
        }

        std::wstring GpsDmsText(double d, double m, double s, wchar_t ref) const
        {
            std::wstringstream ss;
            ss << static_cast<int>(std::llround(d)) << L"\u00B0 "
               << static_cast<int>(std::llround(m)) << L"' "
               << std::fixed << std::setprecision(2) << s << L"\" "
               << ref;
            return ss.str();
        }
    };

    std::wstring EntryStringByTag(const ExifReader& r, const std::vector<size_t>& entries, uint16_t wanted)
    {
        for (size_t entry : entries)
        {
            uint16_t tag = 0, type = 0;
            uint32_t count = 0;
            if (r.EntryHeader(entry, tag, type, count) && tag == wanted)
                return r.ReadAscii(entry);
        }
        return L"";
    }

    bool EntryNumberByTag(const ExifReader& r, const std::vector<size_t>& entries, uint16_t wanted, int& out)
    {
        for (size_t entry : entries)
        {
            uint16_t tag = 0, type = 0;
            uint32_t count = 0;
            if (r.EntryHeader(entry, tag, type, count) && tag == wanted)
                return r.ReadNumber(entry, out);
        }
        return false;
    }

    bool EntryLongByTag(const ExifReader& r, const std::vector<size_t>& entries, uint16_t wanted, uint32_t& out)
    {
        for (size_t entry : entries)
        {
            uint16_t tag = 0, type = 0;
            uint32_t count = 0;
            if (r.EntryHeader(entry, tag, type, count) && tag == wanted)
                return r.ReadLongValue(entry, out);
        }
        return false;
    }

    bool EntryRationalByTag(const ExifReader& r, const std::vector<size_t>& entries, uint16_t wanted, double& out)
    {
        for (size_t entry : entries)
        {
            uint16_t tag = 0, type = 0;
            uint32_t count = 0;
            if (r.EntryHeader(entry, tag, type, count) && tag == wanted)
                return r.ReadRational(entry, out);
        }
        return false;
    }

    void ParseExifPayloadIntoMetadata(const std::vector<uint8_t>& payload, MetadataInfo& info, std::wstringstream& notes)
    {
        ExifReader r(payload);
        if (!r.valid)
        {
            notes << L"libheif EXIF: bloque encontrado, pero no se localizo cabecera TIFF.\r\n";
            return;
        }

        uint32_t ifd0Rel = 0;
        if (!r.Read32Abs(r.tiff + 4, ifd0Rel))
        {
            notes << L"libheif EXIF: no se pudo leer offset IFD0.\r\n";
            return;
        }

        std::vector<size_t> ifd0;
        if (!r.IfdEntries(ifd0Rel, ifd0))
        {
            notes << L"libheif EXIF: no se pudo leer IFD0.\r\n";
            return;
        }

        auto setIfEmpty = [](std::wstring& dst, const std::wstring& value)
        {
            if (dst.empty() && !value.empty())
                dst = value;
        };

        setIfEmpty(info.make, EntryStringByTag(r, ifd0, 0x010F));
        setIfEmpty(info.model, EntryStringByTag(r, ifd0, 0x0110));
        setIfEmpty(info.software, EntryStringByTag(r, ifd0, 0x0131));
        setIfEmpty(info.dateOriginal, EntryStringByTag(r, ifd0, 0x0132));

        int orientation = 0;
        if (info.orientation.empty() && EntryNumberByTag(r, ifd0, 0x0112, orientation))
            info.orientation = std::to_wstring(orientation);

        uint32_t exifRel = 0;
        if (EntryLongByTag(r, ifd0, 0x8769, exifRel))
        {
            std::vector<size_t> exif;
            if (r.IfdEntries(exifRel, exif))
            {
                setIfEmpty(info.dateOriginal, EntryStringByTag(r, exif, 0x9003));

                double value = 0.0;
                if (info.exposureTime.empty() && EntryRationalByTag(r, exif, 0x829A, value))
                    info.exposureTime = FormatExposureTime(value);
                if (info.fNumber.empty() && EntryRationalByTag(r, exif, 0x829D, value))
                    info.fNumber = FormatFNumber(value);
                if (info.focalLength.empty() && EntryRationalByTag(r, exif, 0x920A, value))
                    info.focalLength = FormatFocalLength(value);
                if (info.digitalZoomRatio.empty() && EntryRationalByTag(r, exif, 0xA404, value))
                    info.digitalZoomRatio = FormatDouble(value, 2) + L"x";

                int number = 0;
                if (info.isoSpeed.empty() && EntryNumberByTag(r, exif, 0x8827, number))
                    info.isoSpeed = std::to_wstring(number);
                if (info.isoSpeed.empty() && EntryNumberByTag(r, exif, 0x8833, number))
                    info.isoSpeed = std::to_wstring(number);
                if (info.focalLength35mm.empty() && EntryNumberByTag(r, exif, 0xA405, number))
                    info.focalLength35mm = std::to_wstring(number);
                if (info.exposureMode.empty() && EntryNumberByTag(r, exif, 0xA402, number))
                    info.exposureMode = ExposureModeName(number);
                if (info.meteringMode.empty() && EntryNumberByTag(r, exif, 0x9207, number))
                    info.meteringMode = MeteringModeName(number);
                if (info.whiteBalance.empty() && EntryNumberByTag(r, exif, 0xA403, number))
                    info.whiteBalance = WhiteBalanceName(number);
                if (info.flash.empty() && EntryNumberByTag(r, exif, 0x9209, number))
                    info.flash = FlashText(number);

                setIfEmpty(info.lensMake, EntryStringByTag(r, exif, 0xA433));
                setIfEmpty(info.lensModel, EntryStringByTag(r, exif, 0xA434));
            }
        }

        uint32_t gpsRel = 0;
        if (EntryLongByTag(r, ifd0, 0x8825, gpsRel))
        {
            std::vector<size_t> gps;
            if (r.IfdEntries(gpsRel, gps))
            {
                std::wstring latRef = EntryStringByTag(r, gps, 0x0001);
                std::wstring lonRef = EntryStringByTag(r, gps, 0x0003);

                double latD = 0.0, latM = 0.0, latS = 0.0;
                double lonD = 0.0, lonM = 0.0, lonS = 0.0;
                bool gotLat = false;
                bool gotLon = false;

                for (size_t entry : gps)
                {
                    uint16_t tag = 0, type = 0;
                    uint32_t count = 0;
                    if (!r.EntryHeader(entry, tag, type, count))
                        continue;

                    if (tag == 0x0002)
                        gotLat = r.ReadGpsTriple(entry, latD, latM, latS);
                    if (tag == 0x0004)
                        gotLon = r.ReadGpsTriple(entry, lonD, lonM, lonS);
                }

                if (gotLat && gotLon)
                {
                    double lat = latD + (latM / 60.0) + (latS / 3600.0);
                    double lon = lonD + (lonM / 60.0) + (lonS / 3600.0);

                    wchar_t latRefChar = latRef.empty() ? L'N' : latRef[0];
                    wchar_t lonRefChar = lonRef.empty() ? L'E' : lonRef[0];

                    if (latRefChar == L'S' || latRefChar == L's')
                        lat = -lat;
                    if (lonRefChar == L'W' || lonRefChar == L'w')
                        lon = -lon;

                    if (info.gpsLatitude.empty())
                        info.gpsLatitude = FormatDouble(lat, 7);
                    if (info.gpsLongitude.empty())
                        info.gpsLongitude = FormatDouble(lon, 7);
                    if (info.gpsRaw.empty())
                    {
                        info.gpsRaw = L"lat=" + r.GpsDmsText(latD, latM, latS, latRefChar) +
                                      L" | lon=" + r.GpsDmsText(lonD, lonM, lonS, lonRefChar);
                    }
                }
            }
        }

        notes << L"libheif EXIF: parse TIFF interno aplicado";
        if (!info.make.empty() || !info.model.empty())
            notes << L" (" << CleanLine(info.make + L" " + info.model) << L")";
        notes << L".\r\n";
    }

    void AddMetadataBlockSummary(heif_image_handle* handle, MetadataInfo& metadata)
    {
        if (!handle)
            return;

        const int total = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);
        int exifCount = 0;
        int xmpCount = 0;

        if (total > 0)
        {
            std::vector<heif_item_id> ids(static_cast<size_t>(total));
            const int written = heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(), total);

            std::wstringstream notes;
            notes << L"libheif: bloques de metadatos=" << written << L".\r\n";

            for (int i = 0; i < written; ++i)
            {
                const heif_item_id id = ids[static_cast<size_t>(i)];
                const char* type = heif_image_handle_get_metadata_type(handle, id);
                const char* contentType = heif_image_handle_get_metadata_content_type(handle, id);
                const size_t size = heif_image_handle_get_metadata_size(handle, id);

                std::wstring wType = Utf8ToWide(type);
                std::wstring wContent = Utf8ToWide(contentType);

                notes << L"libheif metadata[" << i << L"]: type="
                      << (wType.empty() ? L"No disponible" : wType)
                      << L" content="
                      << (wContent.empty() ? L"No disponible" : wContent)
                      << L" size=" << size << L" bytes.\r\n";

                if (size > 0 && size <= 64ull * 1024ull * 1024ull)
                {
                    std::vector<uint8_t> payload(size);
                    heif_error err = heif_image_handle_get_metadata(handle, id, payload.data());
                    if (Ok(err))
                    {
                        if (wType == L"Exif")
                        {
                            ++exifCount;
                            ParseExifPayloadIntoMetadata(payload, metadata, notes);
                        }
                        else if (wType == L"mime" && (wContent.find(L"xml") != std::wstring::npos || wContent.find(L"rdf") != std::wstring::npos))
                        {
                            ++xmpCount;
                        }
                    }
                    else
                    {
                        notes << L"libheif metadata[" << i << L"]: no se pudo leer payload.\r\n";
                    }
                }
            }

            notes << L"Resumen libheif: EXIF=" << exifCount << L", XMP=" << xmpCount << L".\r\n";
            metadata.containerNotes += notes.str();
        }
        else
        {
            metadata.containerNotes += L"libheif: no expone bloques de metadatos para esta imagen.\r\n";
        }
    }
}

bool HeifDecoder::LoadFromMemory(
    const std::wstring& path,
    const std::wstring& extension,
    ULONGLONG fileSize,
    FILETIME created,
    FILETIME modified,
    const std::vector<BYTE>& raw,
    LoadedImage& outImage,
    std::wstring& error)
{
    if (raw.empty())
    {
        error = L"HEIC/HEIF: archivo vacio.";
        return false;
    }

    heif_context* ctx = heif_context_alloc();
    if (!ctx)
    {
        error = L"HEIC/HEIF: no se pudo crear contexto libheif.";
        return false;
    }

    heif_error err = heif_context_read_from_memory_without_copy(ctx, raw.data(), raw.size(), nullptr);
    if (!Ok(err))
    {
        error = HeifErrorText(err, L"HEIC/HEIF: libheif no pudo leer el contenedor");
        heif_context_free(ctx);
        return false;
    }

    heif_image_handle* handle = nullptr;
    err = heif_context_get_primary_image_handle(ctx, &handle);
    if (!Ok(err) || !handle)
    {
        error = HeifErrorText(err, L"HEIC/HEIF: no se pudo obtener la imagen primaria");
        heif_context_free(ctx);
        return false;
    }

    const int w = heif_image_handle_get_width(handle);
    const int h = heif_image_handle_get_height(handle);
    if (w <= 0 || h <= 0)
    {
        error = L"HEIC/HEIF: dimensiones invalidas.";
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return false;
    }

    heif_decoding_options* options = heif_decoding_options_alloc();
    heif_image* image = nullptr;

    err = heif_decode_image(
        handle,
        &image,
        heif_colorspace_RGB,
        heif_chroma_interleaved_RGBA,
        options);

    if (options)
        heif_decoding_options_free(options);

    if (!Ok(err) || !image)
    {
        error = HeifErrorText(err, L"HEIC/HEIF: libheif no pudo decodificar pixeles");
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return false;
    }

    int srcStride = 0;
    const uint8_t* src = heif_image_get_plane_readonly(image, heif_channel_interleaved, &srcStride);
    if (!src || srcStride <= 0)
    {
        error = L"HEIC/HEIF: libheif no devolvio plano RGBA valido.";
        heif_image_release(image);
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return false;
    }

    LoadedImage img;
    img.path = path;
    img.fileName = FileNameFromPathLocal(path);
    img.extension = extension;
    img.width = static_cast<UINT>(w);
    img.height = static_cast<UINT>(h);
    img.originalWidth = img.width;
    img.originalHeight = img.height;
    img.preview = false;
    img.stride = img.width * 4;
    img.fileSize = fileSize;
    img.created = created;
    img.modified = modified;
    img.pixelsBGRA.resize(static_cast<size_t>(img.stride) * img.height);

    for (UINT y = 0; y < img.height; ++y)
    {
        const uint8_t* row = src + static_cast<size_t>(srcStride) * y;
        BYTE* dst = img.pixelsBGRA.data() + static_cast<size_t>(img.stride) * y;

        for (UINT x = 0; x < img.width; ++x)
        {
            const uint8_t r = row[x * 4 + 0];
            const uint8_t g = row[x * 4 + 1];
            const uint8_t b = row[x * 4 + 2];
            const uint8_t a = row[x * 4 + 3];

            dst[x * 4 + 0] = b;
            dst[x * 4 + 1] = g;
            dst[x * 4 + 2] = r;
            dst[x * 4 + 3] = a;
        }
    }

    img.metadata.formatName = (extension == L".heif") ? L"HEIF" : L"HEIC";
    img.metadata.pixelFormat = L"libheif RGBA -> BGRA";
    img.metadata.bitsPerPixel = 32;
    img.metadata.containerNotes = L"HEIF/HEIC decodificado mediante libheif.\r\n";
    img.metadata.warning = L"HEIC/HEIF abierto mediante libheif. Los metadatos EXIF/XMP se leen desde los bloques internos del contenedor cuando existen.";
    AddMetadataBlockSummary(handle, img.metadata);

    img.hashes = HashService::ComputeAll(raw);

    heif_image_release(image);
    heif_image_handle_release(handle);
    heif_context_free(ctx);

    outImage = std::move(img);
    return true;
}

