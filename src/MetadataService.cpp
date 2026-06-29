#include "MetadataService.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cmath>

#pragma comment(lib, "windowscodecs.lib")

namespace
{
    bool SilentPixelMarkerIsTrue(const std::wstring& text, const wchar_t* marker)
    {
        const std::wstring key(marker);
        size_t pos = 0;
        while ((pos = text.find(key, pos)) != std::wstring::npos)
        {
            const size_t valuePos = pos + key.size();

            if (valuePos < text.size())
            {
                const wchar_t value = text[valuePos];
                if (value == L'1')
                    return true;
                if (value == L'0')
                {
                    pos = valuePos + 1;
                    continue;
                }
            }

            pos = valuePos;
        }

        return false;
    }

    std::wstring SilentPixelBoolWord(bool value)
    {
        return value ? L"sÃ­" : L"no";
    }
    bool SilentPixelIccDetectedForQuick(const std::wstring& text)
    {
        return
            SilentPixelMarkerIsTrue(text, L"ICC=") ||
            text.find(L"ICC profile APP2 detectado") != std::wstring::npos ||
            text.find(L"ICC profile") != std::wstring::npos ||
            text.find(L"ICC_PROFILE") != std::wstring::npos;
    }


    template <typename T>
    void SafeRelease(T*& p)
    {
        if (p)
        {
            p->Release();
            p = nullptr;
        }
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

    std::wstring ToLowerCopy(std::wstring s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return s;
    }

    bool ContainsText(const std::wstring& text, const std::wstring& needle)
    {
        return ToLowerCopy(text).find(ToLowerCopy(needle)) != std::wstring::npos;
    }

    bool ShouldSkipMetadataEntry(const std::wstring& key, const std::wstring& value)
    {
        if (value.empty())
            return true;

        if (value.rfind(L"Tipo PROPVARIANT", 0) == 0)
            return true;

        if (ContainsText(key, L"/chrominance") || ContainsText(key, L"/luminance") || ContainsText(key, L"TableEntry"))
            return true;

        if (ContainsText(key, L"/app1") && value == L"valor")
            return true;

        return false;
    }

    bool StartsWithBytes(const BYTE* data, size_t size, const char* magic, size_t magicSize)
    {
        return size >= magicSize && memcmp(data, magic, magicSize) == 0;
    }

    double Rational64ToDouble(const ULARGE_INTEGER& v)
    {
        const double numerator = static_cast<double>(v.LowPart);
        const double denominator = static_cast<double>(v.HighPart);

        if (denominator > 0.0)
            return numerator / denominator;

        if (numerator > 0.0)
            return numerator;

        return 0.0;
    }

    double SignedRational64ToDouble(const LARGE_INTEGER& v)
    {
        const double numerator = static_cast<double>(static_cast<int>(v.LowPart));
        const double denominator = static_cast<double>(v.HighPart);

        if (denominator != 0.0)
            return numerator / denominator;

        if (numerator != 0.0)
            return numerator;

        return 0.0;
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

    bool TryRationalToDouble(const PROPVARIANT& pv, double& out)
    {
        out = 0.0;

        switch (pv.vt)
        {
        case VT_UI8:
            out = Rational64ToDouble(pv.uhVal);
            return out > 0.0 || pv.uhVal.QuadPart != 0;
        case VT_I8:
            out = SignedRational64ToDouble(pv.hVal);
            return true;
        case VT_UI4:
            out = static_cast<double>(pv.ulVal);
            return true;
        case VT_I4:
            out = static_cast<double>(pv.lVal);
            return true;
        case VT_UI2:
            out = static_cast<double>(pv.uiVal);
            return true;
        case VT_I2:
            out = static_cast<double>(pv.iVal);
            return true;
        case VT_R4:
            out = static_cast<double>(pv.fltVal);
            return true;
        case VT_R8:
            out = pv.dblVal;
            return true;
        default:
            break;
        }

        if (pv.vt & VT_VECTOR)
        {
            const VARTYPE base = pv.vt & ~VT_VECTOR;
            if (base == VT_UI8 && pv.cauh.cElems > 0)
            {
                out = Rational64ToDouble(pv.cauh.pElems[0]);
                return true;
            }
            if (base == VT_I8 && pv.cah.cElems > 0)
            {
                out = SignedRational64ToDouble(pv.cah.pElems[0]);
                return true;
            }
            if (base == VT_UI4 && pv.caul.cElems > 0)
            {
                out = static_cast<double>(pv.caul.pElems[0]);
                return true;
            }
            if (base == VT_UI2 && pv.caui.cElems > 0)
            {
                out = static_cast<double>(pv.caui.pElems[0]);
                return true;
            }
        }

        return false;
    }

    bool QueryDouble(IWICMetadataQueryReader* reader, LPCWSTR name, double& out)
    {
        if (!reader)
            return false;

        PROPVARIANT pv{};
        PropVariantInit(&pv);
        bool ok = false;
        if (SUCCEEDED(reader->GetMetadataByName(name, &pv)))
            ok = TryRationalToDouble(pv, out);
        PropVariantClear(&pv);
        return ok;
    }

    std::wstring QueryFirstIntegerText(IWICMetadataQueryReader* reader, LPCWSTR name)
    {
        double value = 0.0;
        if (!QueryDouble(reader, name, value))
            return L"";
        return std::to_wstring(static_cast<int>(std::llround(value)));
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

        return FormatDouble(seconds, seconds < 10.0 ? 2 : 1) + L" s";
    }

    std::wstring FormatFNumber(double value)
    {
        if (value <= 0.0)
            return L"";
        return L"f/" + FormatDouble(value, 1);
    }

    std::wstring FormatFocalLength(double value)
    {
        if (value <= 0.0)
            return L"";
        return FormatDouble(value, 1) + L" mm";
    }

    bool TryParseDoubleText(const std::wstring& text, double& out)
    {
        try
        {
            size_t idx = 0;
            out = std::stod(text, &idx);
            return idx > 0;
        }
        catch (...)
        {
            out = 0.0;
            return false;
        }
    }

    std::wstring FormatGpsDms(double decimal, bool latitude)
    {
        const bool negative = decimal < 0.0;
        double value = std::fabs(decimal);

        int degrees = static_cast<int>(std::floor(value));
        double minutesFull = (value - static_cast<double>(degrees)) * 60.0;
        int minutes = static_cast<int>(std::floor(minutesFull));
        double seconds = (minutesFull - static_cast<double>(minutes)) * 60.0;

        if (seconds >= 59.995)
        {
            seconds = 0.0;
            ++minutes;
            if (minutes >= 60)
            {
                minutes = 0;
                ++degrees;
            }
        }

        const wchar_t ref = latitude
            ? (negative ? L'S' : L'N')
            : (negative ? L'W' : L'E');

        std::wstringstream ss;
        ss << degrees << L"\u00B0 " << minutes << L"' "
           << std::fixed << std::setprecision(2) << seconds << L"\" " << ref;
        return ss.str();
    }

    std::wstring BuildGpsRawDms(const std::wstring& latitudeText, const std::wstring& longitudeText)
    {
        double lat = 0.0;
        double lon = 0.0;
        if (!TryParseDoubleText(latitudeText, lat) || !TryParseDoubleText(longitudeText, lon))
            return L"";

        return L"lat=" + FormatGpsDms(lat, true) + L" | lon=" + FormatGpsDms(lon, false);
    }


    std::wstring ExposureModeName(const std::wstring& value)
    {
        if (value == L"0") return L"Auto";
        if (value == L"1") return L"Manual";
        if (value == L"2") return L"Auto bracket";
        return value;
    }

    std::wstring MeteringModeName(const std::wstring& value)
    {
        if (value == L"1") return L"Media";
        if (value == L"2") return L"Ponderada al centro";
        if (value == L"3") return L"Puntual";
        if (value == L"4") return L"Multipunto";
        if (value == L"5") return L"Patrón/matriz";
        if (value == L"6") return L"Parcial";
        if (value == L"255") return L"Otro";
        return value;
    }

    std::wstring WhiteBalanceName(const std::wstring& value)
    {
        if (value == L"0") return L"Auto";
        if (value == L"1") return L"Manual";
        return value;
    }

    std::wstring FlashText(const std::wstring& value)
    {
        if (value.empty())
            return L"";
        try
        {
            const int raw = std::stoi(value);
            return (raw & 1) ? L"Disparado" : L"No disparado";
        }
        catch (...)
        {
            return value;
        }
    }

    std::wstring FindEntryValueByTag(const std::vector<MetadataEntry>& entries, const std::wstring& tag)
    {
        const std::wstring needle = L"{ushort=" + tag + L"}";
        for (const auto& entry : entries)
        {
            if (entry.key.find(needle) != std::wstring::npos && !entry.value.empty())
                return entry.value;
        }
        return L"";
    }

    std::wstring FirstNonEmpty(std::initializer_list<std::wstring> values)
    {
        for (const auto& value : values)
        {
            if (!value.empty())
                return value;
        }
        return L"";
    }

    std::wstring RationalPairTextToDoubleText(const std::wstring& value)
    {
        // WIC a veces enumera racionales como "[10, 1]" o "[55, 10]".
        const size_t lb = value.find(L'[');
        const size_t comma = value.find(L',');
        const size_t rb = value.find(L']');
        if (lb == std::wstring::npos || comma == std::wstring::npos || rb == std::wstring::npos || !(lb < comma && comma < rb))
            return L"";

        try
        {
            const double num = std::stod(value.substr(lb + 1, comma - lb - 1));
            const double den = std::stod(value.substr(comma + 1, rb - comma - 1));
            if (den == 0.0)
                return L"";

            return FormatDouble(num / den, 4);
        }
        catch (...)
        {
            return L"";
        }
    }

    std::wstring RationalPairTextToExposure(const std::wstring& value)
    {
        const std::wstring secondsText = RationalPairTextToDoubleText(value);
        if (secondsText.empty())
            return L"";

        try
        {
            return FormatExposureTime(std::stod(secondsText));
        }
        catch (...)
        {
            return L"";
        }
    }

    std::wstring RationalPairTextToFNumber(const std::wstring& value)
    {
        const std::wstring fText = RationalPairTextToDoubleText(value);
        if (fText.empty())
            return L"";

        try
        {
            return FormatFNumber(std::stod(fText));
        }
        catch (...)
        {
            return L"";
        }
    }

    std::wstring RationalPairTextToFocalLength(const std::wstring& value)
    {
        const std::wstring focalText = RationalPairTextToDoubleText(value);
        if (focalText.empty())
            return L"";

        try
        {
            return FormatFocalLength(std::stod(focalText));
        }
        catch (...)
        {
            return L"";
        }
    }

    bool LooksLikeIsoBmffHeif(const std::vector<BYTE>& rawBytes, std::wstring& brand)
    {
        if (rawBytes.size() < 12)
            return false;

        auto readAscii = [&](size_t pos, size_t len) -> std::wstring
        {
            std::wstring out;
            if (pos + len > rawBytes.size())
                return out;
            for (size_t i = 0; i < len; ++i)
                out.push_back(static_cast<wchar_t>(rawBytes[pos + i]));
            return out;
        };

        const std::wstring box = readAscii(4, 4);
        if (box != L"ftyp")
            return false;

        const std::wstring major = readAscii(8, 4);
        const std::wstring compatible = readAscii(16, rawBytes.size() > 64 ? 48 : (rawBytes.size() > 16 ? rawBytes.size() - 16 : 0));

        if (major == L"heic" || major == L"heix" || major == L"hevc" || major == L"hevx" ||
            major == L"mif1" || major == L"msf1" ||
            compatible.find(L"heic") != std::wstring::npos ||
            compatible.find(L"heix") != std::wstring::npos ||
            compatible.find(L"hevc") != std::wstring::npos ||
            compatible.find(L"hevx") != std::wstring::npos ||
            compatible.find(L"mif1") != std::wstring::npos)
        {
            brand = major;
            return true;
        }

        return false;
    }

    void ApplyFlatWicFallbacks(MetadataInfo& info)
    {
        // Fallback para JPEG/HEIC cuando WIC no expone /app1/ifd/exif clÃ¡sico,
        // pero sÃ­ enumera tags planos en entries: /app1//{ushort=0}//{ushort=33434}, etc.
        if (info.make.empty())
            info.make = FindEntryValueByTag(info.entries, L"271");
        if (info.model.empty())
            info.model = FindEntryValueByTag(info.entries, L"272");
        if (info.software.empty())
            info.software = FindEntryValueByTag(info.entries, L"305");
        if (info.dateOriginal.empty())
            info.dateOriginal = FirstNonEmpty({
                FindEntryValueByTag(info.entries, L"36867"),
                FindEntryValueByTag(info.entries, L"306")
            });

        if (info.exposureTime.empty())
            info.exposureTime = RationalPairTextToExposure(FindEntryValueByTag(info.entries, L"33434"));
        if (info.fNumber.empty())
            info.fNumber = RationalPairTextToFNumber(FindEntryValueByTag(info.entries, L"33437"));
        if (info.isoSpeed.empty())
            info.isoSpeed = FindEntryValueByTag(info.entries, L"34855");
        if (info.focalLength.empty())
            info.focalLength = RationalPairTextToFocalLength(FindEntryValueByTag(info.entries, L"37386"));
        if (info.focalLength35mm.empty())
            info.focalLength35mm = FindEntryValueByTag(info.entries, L"41989");
        if (info.exposureMode.empty())
            info.exposureMode = ExposureModeName(FindEntryValueByTag(info.entries, L"41986"));
        if (info.meteringMode.empty())
            info.meteringMode = MeteringModeName(FindEntryValueByTag(info.entries, L"37383"));
        if (info.whiteBalance.empty())
            info.whiteBalance = WhiteBalanceName(FindEntryValueByTag(info.entries, L"41987"));
        if (info.flash.empty())
            info.flash = FlashText(FindEntryValueByTag(info.entries, L"37385"));
        if (info.digitalZoomRatio.empty())
        {
            const std::wstring zoom = RationalPairTextToDoubleText(FindEntryValueByTag(info.entries, L"41988"));
            if (!zoom.empty())
                info.digitalZoomRatio = zoom + L"x";
        }
        if (info.lensMake.empty())
            info.lensMake = FindEntryValueByTag(info.entries, L"42035");
        if (info.lensModel.empty())
            info.lensModel = FindEntryValueByTag(info.entries, L"42036");
        if (info.orientation.empty())
            info.orientation = FindEntryValueByTag(info.entries, L"274");
    }

    std::wstring MarkerName(BYTE marker)
    {
        switch (marker)
        {
        case 0xE0: return L"APP0/JFIF";
        case 0xE1: return L"APP1/EXIF-XMP";
        case 0xE2: return L"APP2/ICC";
        case 0xED: return L"APP13/IPTC";
        case 0xFE: return L"Comentario JPEG";
        default:
        {
            std::wstringstream ss;
            ss << L"JPEG marker 0x" << std::hex << std::uppercase << static_cast<int>(marker);
            return ss.str();
        }
        }
    }
}

bool MetadataInfo::HasGps() const
{
    return !gpsLatitude.empty() && !gpsLongitude.empty();
}

bool MetadataInfo::HasShootingData() const
{
    return !exposureTime.empty()
        || !fNumber.empty()
        || !isoSpeed.empty()
        || !exposureMode.empty()
        || !focalLength.empty()
        || !focalLength35mm.empty()
        || !lensMake.empty()
        || !lensModel.empty()
        || !flash.empty()
        || !meteringMode.empty()
        || !whiteBalance.empty()
        || !digitalZoomRatio.empty();
}

std::wstring MetadataInfo::GpsText() const
{
    if (!HasGps())
        return L"";
    return gpsLatitude + L", " + gpsLongitude;
}

std::wstring MetadataInfo::ExifOrientationText(bool includeRawValue) const
{
    const std::wstring raw = CleanLine(orientation);
    if (raw.empty())
        return L"No disponible";

    int value = 0;
    bool hasValue = false;

    try
    {
        size_t pos = raw.find_first_of(L"0123456789");
        if (pos != std::wstring::npos)
        {
            size_t used = 0;
            value = std::stoi(raw.substr(pos), &used);
            hasValue = used > 0;
        }
    }
    catch (...)
    {
        value = 0;
        hasValue = false;
    }

    const wchar_t* label = nullptr;
    switch (value)
    {
    case 1: label = L"normal"; break;
    case 2: label = L"volteada horizontal"; break;
    case 3: label = L"rotada 180°"; break;
    case 4: label = L"volteada vertical"; break;
    case 5: label = L"transpuesta"; break;
    case 6: label = L"rotada 90° derecha"; break;
    case 7: label = L"transversa"; break;
    case 8: label = L"rotada 90° izquierda"; break;
    default: break;
    }

    if (label)
    {
        if (includeRawValue)
            return std::wstring(label) + L" (valor EXIF: " + std::to_wstring(value) + L")";
        return label;
    }

    if (hasValue)
    {
        if (includeRawValue)
            return L"desconocida (valor EXIF: " + std::to_wstring(value) + L")";
        return L"desconocida";
    }

    return raw;
}

std::wstring MetadataInfo::HumanSummary() const
{
    std::wstringstream ss;

    auto section = [](std::wstringstream& out, const std::wstring& title)
    {
        out << title << L"\r\n";
    };

    section(ss, L"Resumen principal");
    ss << L"Dispositivo: " << (make.empty() && model.empty() ? L"No disponible" : CleanLine(make + L" " + model)) << L"\r\n";
    ss << L"Software: " << (software.empty() ? L"No disponible" : CleanLine(software)) << L"\r\n";
    ss << L"Fecha original: " << (dateOriginal.empty() ? L"No disponible" : CleanLine(dateOriginal)) << L"\r\n";
    if (HasShootingData())
    {
        ss << L"Disparo: ";
        bool first = true;
        auto add = [&](const std::wstring& label, const std::wstring& value)
        {
            if (value.empty()) return;
            if (!first) ss << L" | ";
            ss << label << L": " << value;
            first = false;
        };
        add(L"ISO", isoSpeed);
        add(L"Diafragma", fNumber);
        add(L"Velocidad", exposureTime);
        add(L"Focal", focalLength);
        ss << L"\r\n";
    }
    ss << L"Coordenadas: " << (HasGps() ? GpsText() : L"No disponible") << L"\r\n";
    if (HasGps())
        ss << L"Aviso: esta imagen contiene ubicación. SilentPixel no abre mapas ni llama APIs externas; usa Copiar ubicación si quieres pegarlas manualmente.\r\n";
    return ss.str();
}

std::wstring MetadataInfo::FullText() const
{
    std::wstringstream ss;

    auto section = [](std::wstringstream& out, const std::wstring& title)
    {
        out << title << L"\r\n";
    };

    ss << HumanSummary() << L"\r\n";

    section(ss, L"Datos t\u00E9cnicos");
    ss << L"Formato: " << (formatName.empty() ? L"No disponible" : formatName) << L"\r\n";
    ss << L"Formato de p\u00EDxel: " << (pixelFormat.empty() ? L"No disponible" : pixelFormat) << L"\r\n";
    ss << L"Profundidad estimada: " << (bitsPerPixel == 0 ? L"No disponible" : std::to_wstring(bitsPerPixel) + L" bpp") << L"\r\n";
    ss << L"Orientación EXIF: " << ExifOrientationText(true) << L"\r\n";
    ss << L"Perfil de color: " << (colorProfile.empty() ? L"No detectado" : colorProfile) << L"\r\n";
    if (!gpsRaw.empty()) ss << L"Coordenadas bruto: " << gpsRaw << L"\r\n";
    if (!warning.empty()) ss << L"Aviso: " << warning << L"\r\n";

    if (HasShootingData())
    {
        ss << L"\r\n";
        section(ss, L"Par\u00E1metros de disparo");
        ss << L"ISO: " << (isoSpeed.empty() ? L"No disponible" : isoSpeed) << L"\r\n";
        ss << L"Diafragma: " << (fNumber.empty() ? L"No disponible" : fNumber) << L"\r\n";
        ss << L"Velocidad obturaci\u00F3n: " << (exposureTime.empty() ? L"No disponible" : exposureTime) << L"\r\n";
        ss << L"Longitud focal: " << (focalLength.empty() ? L"No disponible" : focalLength) << L"\r\n";
        ss << L"Equivalente 35 mm: " << (focalLength35mm.empty() ? L"No disponible" : focalLength35mm + L" mm") << L"\r\n";
        ss << L"Modo exposici\u00F3n: " << (exposureMode.empty() ? L"No disponible" : exposureMode) << L"\r\n";
        ss << L"Medici\u00F3n: " << (meteringMode.empty() ? L"No disponible" : meteringMode) << L"\r\n";
        ss << L"Balance blancos: " << (whiteBalance.empty() ? L"No disponible" : whiteBalance) << L"\r\n";
        ss << L"Flash: " << (flash.empty() ? L"No disponible" : flash) << L"\r\n";
        ss << L"Lente: " << (lensMake.empty() && lensModel.empty() ? L"No disponible" : CleanLine(lensMake + L" " + lensModel)) << L"\r\n";
        if (!digitalZoomRatio.empty())
            ss << L"Zoom digital: " << digitalZoomRatio << L"\r\n";
    }

    if (!containerNotes.empty())
    {
        ss << L"\r\n";
        section(ss, L"Contenedor");
        ss << containerNotes << L"\r\n";
    }

    if (!entries.empty())
    {
        ss << L"\r\n";
        section(ss, L"Lectura WIC avanzada, compacta");

        size_t shown = 0;
        for (const auto& e : entries)
        {
            if (shown >= 80)
                break;

            ss << (e.section.empty() ? L"General" : e.section) << L" | " << e.key << L": " << e.value << L"\r\n";
            ++shown;
        }

        if (entries.size() > shown)
            {}
    }
    else
    {
        ss << L"\r\n";
        section(ss, L"Lectura WIC avanzada, compacta");
        ss << L"No se han encontrado entradas adicionales \u00FAtiles, o WIC no las expone para este archivo.\r\n";
    }

    return ss.str();
}

MetadataInfo MetadataService::Extract(IWICBitmapDecoder* decoder, IWICBitmapFrameDecode* frame, const std::wstring& extension, const std::vector<BYTE>& rawBytes)
{
    MetadataInfo info;
    info.formatName = ExtensionToFormat(extension);

    WICPixelFormatGUID pixelGuid{};
    if (frame && SUCCEEDED(frame->GetPixelFormat(&pixelGuid)))
    {
        info.pixelFormat = GuidToName(pixelGuid);
        info.bitsPerPixel = BitsPerPixelFromFormat(pixelGuid);
    }

    if (decoder)
    {
        UINT count = 0;
        if (SUCCEEDED(decoder->GetColorContexts(0, nullptr, &count)) && count > 0)
            info.colorProfile = L"Detectado (" + std::to_wstring(count) + L")";
    }

    IWICMetadataQueryReader* reader = nullptr;
    if (frame && SUCCEEDED(frame->GetMetadataQueryReader(&reader)) && reader)
    {
        info.make = QueryString(reader, L"/app1/ifd/{ushort=271}");
        info.model = QueryString(reader, L"/app1/ifd/{ushort=272}");
        info.software = QueryString(reader, L"/app1/ifd/{ushort=305}");
        info.dateOriginal = QueryString(reader, L"/app1/ifd/exif/{ushort=36867}");
        if (info.dateOriginal.empty())
            info.dateOriginal = QueryString(reader, L"/app1/ifd/{ushort=306}");

        info.orientation = QueryAny(reader, L"/app1/ifd/{ushort=274}");

        double value = 0.0;
        if (QueryDouble(reader, L"/app1/ifd/exif/{ushort=33434}", value))
            info.exposureTime = FormatExposureTime(value);
        if (QueryDouble(reader, L"/app1/ifd/exif/{ushort=33437}", value))
            info.fNumber = FormatFNumber(value);
        info.isoSpeed = QueryFirstIntegerText(reader, L"/app1/ifd/exif/{ushort=34855}");
        if (info.isoSpeed.empty())
            info.isoSpeed = QueryFirstIntegerText(reader, L"/app1/ifd/exif/{ushort=34867}");
        if (QueryDouble(reader, L"/app1/ifd/exif/{ushort=37386}", value))
            info.focalLength = FormatFocalLength(value);
        info.focalLength35mm = QueryFirstIntegerText(reader, L"/app1/ifd/exif/{ushort=41989}");
        info.exposureMode = ExposureModeName(QueryFirstIntegerText(reader, L"/app1/ifd/exif/{ushort=41986}"));
        info.meteringMode = MeteringModeName(QueryFirstIntegerText(reader, L"/app1/ifd/exif/{ushort=37383}"));
        info.whiteBalance = WhiteBalanceName(QueryFirstIntegerText(reader, L"/app1/ifd/exif/{ushort=41987}"));
        info.flash = FlashText(QueryFirstIntegerText(reader, L"/app1/ifd/exif/{ushort=37385}"));
        if (QueryDouble(reader, L"/app1/ifd/exif/{ushort=41988}", value) && value > 0.0)
            info.digitalZoomRatio = FormatDouble(value, 2) + L"x";
        info.lensMake = QueryString(reader, L"/app1/ifd/exif/{ushort=42035}");
        info.lensModel = QueryString(reader, L"/app1/ifd/exif/{ushort=42036}");

        PROPVARIANT lat{};
        PROPVARIANT lon{};
        PropVariantInit(&lat);
        PropVariantInit(&lon);
        if (SUCCEEDED(reader->GetMetadataByName(L"/app1/ifd/gps/{ushort=2}", &lat)))
            info.gpsLatitude = RationalVectorToDecimal(lat);
        if (SUCCEEDED(reader->GetMetadataByName(L"/app1/ifd/gps/{ushort=4}", &lon)))
            info.gpsLongitude = RationalVectorToDecimal(lon);

        const std::wstring latRaw = PropVariantToString(lat);
        const std::wstring lonRaw = PropVariantToString(lon);

        PropVariantClear(&lat);
        PropVariantClear(&lon);

        std::wstring latRef = QueryString(reader, L"/app1/ifd/gps/{ushort=1}");
        std::wstring lonRef = QueryString(reader, L"/app1/ifd/gps/{ushort=3}");
        if (!latRef.empty() && !info.gpsLatitude.empty() && (latRef == L"S" || latRef == L"s"))
            info.gpsLatitude = L"-" + info.gpsLatitude;
        if (!lonRef.empty() && !info.gpsLongitude.empty() && (lonRef == L"W" || lonRef == L"w"))
            info.gpsLongitude = L"-" + info.gpsLongitude;

        info.gpsRaw = BuildGpsRawDms(info.gpsLatitude, info.gpsLongitude);
        if (info.gpsRaw.empty() && (!latRaw.empty() || !lonRaw.empty()))
            info.gpsRaw = L"lat=" + latRaw + L" lon=" + lonRaw;

        EnumerateReader(reader, info.entries, L"WIC", L"", 0);
        ApplyFlatWicFallbacks(info);
        reader->Release();
    }
    else
    {
        info.warning = L"No se pudo abrir el lector de metadatos WIC para esta imagen.";
    }

    ScanRawContainer(rawBytes, extension, info);
    return info;
}

std::wstring MetadataService::QueryString(IWICMetadataQueryReader* reader, LPCWSTR name)
{
    PROPVARIANT pv{};
    PropVariantInit(&pv);
    std::wstring result;
    if (SUCCEEDED(reader->GetMetadataByName(name, &pv)))
    {
        result = PropVariantToString(pv);

        const VARTYPE base = pv.vt & ~VT_VECTOR;
        if ((pv.vt & VT_VECTOR) && base == VT_LPSTR)
            result.clear();
    }
    PropVariantClear(&pv);
    return result;
}

std::wstring MetadataService::QueryAny(IWICMetadataQueryReader* reader, LPCWSTR name)
{
    return QueryString(reader, name);
}

std::wstring MetadataService::PropVariantToString(const PROPVARIANT& pv)
{
    std::wstringstream ss;

    switch (pv.vt)
    {
    case VT_EMPTY: return L"";
    case VT_NULL: return L"null";
    case VT_LPWSTR: return pv.pwszVal ? CleanLine(pv.pwszVal) : L"";
    case VT_BSTR: return pv.bstrVal ? CleanLine(pv.bstrVal) : L"";
    case VT_LPSTR:
        if (pv.pszVal)
        {
            int len = MultiByteToWideChar(CP_UTF8, 0, pv.pszVal, -1, nullptr, 0);
            UINT cp = CP_UTF8;
            if (len <= 0)
            {
                cp = CP_ACP;
                len = MultiByteToWideChar(CP_ACP, 0, pv.pszVal, -1, nullptr, 0);
            }
            if (len > 0)
            {
                std::wstring out(static_cast<size_t>(len), L'\0');
                MultiByteToWideChar(cp, 0, pv.pszVal, -1, out.data(), len);
                while (!out.empty() && out.back() == L'\0') out.pop_back();
                return CleanLine(out);
            }
        }
        return L"";
    case VT_UI1: ss << static_cast<unsigned>(pv.bVal); return ss.str();
    case VT_UI2: ss << pv.uiVal; return ss.str();
    case VT_UI4: ss << pv.ulVal; return ss.str();
    case VT_UI8: ss << pv.uhVal.QuadPart; return ss.str();
    case VT_I1: ss << static_cast<int>(pv.cVal); return ss.str();
    case VT_I2: ss << pv.iVal; return ss.str();
    case VT_I4: ss << pv.lVal; return ss.str();
    case VT_I8: ss << pv.hVal.QuadPart; return ss.str();
    case VT_R4: ss << pv.fltVal; return ss.str();
    case VT_R8: ss << pv.dblVal; return ss.str();
    case VT_BOOL: return pv.boolVal ? L"true" : L"false";
    case VT_CLSID: return pv.puuid ? GuidToName(*pv.puuid) : L"";
    default: break;
    }

    if (pv.vt & VT_VECTOR)
    {
        const VARTYPE base = pv.vt & ~VT_VECTOR;
        ULONG count = 0;
        if (base == VT_UI1) count = pv.caub.cElems;
        else if (base == VT_UI2) count = pv.caui.cElems;
        else if (base == VT_UI4) count = pv.caul.cElems;
        else if (base == VT_UI8) count = pv.cauh.cElems;
        else if (base == VT_LPWSTR) count = pv.calpwstr.cElems;
        else if (base == VT_LPSTR) count = pv.calpstr.cElems;

        ss << L"[";
        const ULONG visible = count > 32 ? 32 : count;
        for (ULONG i = 0; i < visible; ++i)
        {
            if (i) ss << L", ";
            if (base == VT_UI1) ss << static_cast<unsigned>(pv.caub.pElems[i]);
            else if (base == VT_UI2) ss << pv.caui.pElems[i];
            else if (base == VT_UI4) ss << pv.caul.pElems[i];
            else if (base == VT_UI8) ss << pv.cauh.pElems[i].QuadPart;
            else if (base == VT_LPWSTR) ss << (pv.calpwstr.pElems[i] ? pv.calpwstr.pElems[i] : L"");
            else if (base == VT_LPSTR) return L"";
            else ss << L"valor";
        }
        if (count > visible) ss << L", ...";
        ss << L"]";
        return ss.str();
    }

    if (pv.vt == VT_BLOB)
    {
        ss << L"BLOB " << pv.blob.cbSize << L" bytes";
        return ss.str();
    }

    ss << L"Tipo PROPVARIANT 0x" << std::hex << pv.vt;
    return ss.str();
}

std::wstring MetadataService::GuidToName(const GUID& guid)
{
    if (guid == GUID_WICPixelFormat24bppBGR) return L"24bpp BGR";
    if (guid == GUID_WICPixelFormat32bppBGRA) return L"32bpp BGRA";
    if (guid == GUID_WICPixelFormat32bppPBGRA) return L"32bpp PBGRA";
    if (guid == GUID_WICPixelFormat32bppBGR) return L"32bpp BGR";
    if (guid == GUID_WICPixelFormat8bppGray) return L"8bpp Gray";
    if (guid == GUID_WICPixelFormat16bppGray) return L"16bpp Gray";
    if (guid == GUID_WICPixelFormat48bppRGB) return L"48bpp RGB";
    if (guid == GUID_WICPixelFormat64bppRGBA) return L"64bpp RGBA";

    wchar_t buffer[64]{};
    StringFromGUID2(guid, buffer, 64);
    return buffer;
}

void MetadataService::EnumerateReader(IWICMetadataQueryReader* reader, std::vector<MetadataEntry>& entries, const std::wstring& section, const std::wstring& prefix, int depth)
{
    if (!reader || depth > 2)
        return;

    IEnumString* enumerator = nullptr;
    if (FAILED(reader->GetEnumerator(&enumerator)) || !enumerator)
        return;

    LPOLESTR name = nullptr;
    ULONG fetched = 0;

    while (enumerator->Next(1, &name, &fetched) == S_OK && fetched == 1)
    {
        std::wstring queryName = name ? name : L"";

        if (name)
        {
            CoTaskMemFree(name);
            name = nullptr;
        }

        if (queryName.empty())
            continue;

        PROPVARIANT value;
        PropVariantInit(&value);

        if (SUCCEEDED(reader->GetMetadataByName(queryName.c_str(), &value)))
        {
            std::wstring val = PropVariantToString(value);
            std::wstring key = prefix.empty() ? queryName : prefix + L"/" + queryName;

            if (!val.empty() && !ShouldSkipMetadataEntry(key, val))
            {
                if (val.size() > 600)
                    val = val.substr(0, 600) + L" ...";

                entries.push_back({ section, CleanLine(key), CleanLine(val) });
            }

            if (value.vt == VT_UNKNOWN && value.punkVal && depth < 2)
            {
                IWICMetadataQueryReader* nested = nullptr;
                if (SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&nested))) && nested)
                {
                    EnumerateReader(nested, entries, section, key, depth + 1);
                    nested->Release();
                }
            }
        }

        PropVariantClear(&value);
    }

    if (name)
        CoTaskMemFree(name);

    enumerator->Release();
}

std::wstring MetadataService::RationalVectorToDecimal(const PROPVARIANT& pv)
{
    if ((pv.vt & VT_VECTOR) && ((pv.vt & ~VT_VECTOR) == VT_UI8) && pv.cauh.cElems >= 3)
    {
        double deg = Rational64ToDouble(pv.cauh.pElems[0]);
        double min = Rational64ToDouble(pv.cauh.pElems[1]);
        double sec = Rational64ToDouble(pv.cauh.pElems[2]);
        double value = deg + (min / 60.0) + (sec / 3600.0);
        std::wstringstream ss;
        ss << std::fixed << std::setprecision(7) << value;
        return ss.str();
    }
    return L"";
}

std::wstring MetadataService::ExtensionToFormat(const std::wstring& extension)
{
    if (extension == L".jpg" || extension == L".jpeg" || extension == L".jpe") return L"JPEG";
    if (extension == L".jfif" || extension == L".jfi") return L"JFIF/JPEG";
    if (extension == L".png") return L"PNG";
    if (extension == L".bmp") return L"BMP";
    if (extension == L".gif") return L"GIF";
    if (extension == L".tif" || extension == L".tiff") return L"TIFF";
    if (extension == L".ico") return L"ICO";
    if (extension == L".webp") return L"WEBP";
    if (extension == L".heic") return L"HEIC";
    if (extension == L".heif") return L"HEIF";
    return extension;
}

UINT MetadataService::BitsPerPixelFromFormat(const GUID& guid)
{
    if (guid == GUID_WICPixelFormat1bppIndexed) return 1;
    if (guid == GUID_WICPixelFormat2bppIndexed) return 2;
    if (guid == GUID_WICPixelFormat4bppIndexed) return 4;
    if (guid == GUID_WICPixelFormat8bppIndexed || guid == GUID_WICPixelFormat8bppGray) return 8;
    if (guid == GUID_WICPixelFormat16bppGray || guid == GUID_WICPixelFormat16bppBGR555 || guid == GUID_WICPixelFormat16bppBGR565) return 16;
    if (guid == GUID_WICPixelFormat24bppBGR || guid == GUID_WICPixelFormat24bppRGB) return 24;
    if (guid == GUID_WICPixelFormat32bppBGR || guid == GUID_WICPixelFormat32bppBGRA || guid == GUID_WICPixelFormat32bppPBGRA || guid == GUID_WICPixelFormat32bppRGBA) return 32;
    if (guid == GUID_WICPixelFormat48bppRGB) return 48;
    if (guid == GUID_WICPixelFormat64bppRGBA || guid == GUID_WICPixelFormat64bppPRGBA) return 64;
    return 0;
}

void MetadataService::ScanRawContainer(const std::vector<BYTE>& rawBytes, const std::wstring& extension, MetadataInfo& info)
{
    if (rawBytes.empty())
    {
        if (!info.containerNotes.empty())
            info.containerNotes += L"\r\n";
        info.containerNotes += L"Contenedor bruto: pendiente en modo ligero.\r\n";
        return;
    }

    if (extension == L".jpg" || extension == L".jpeg" || extension == L".jpe" || extension == L".jfif" || extension == L".jfi") ScanJpeg(rawBytes, info);
    else if (extension == L".png") ScanPng(rawBytes, info);
    else if (extension == L".bmp") ScanBmp(rawBytes, info);
    else if (extension == L".heic" || extension == L".heif")
    {
        std::wstring brand;
        if (LooksLikeIsoBmffHeif(rawBytes, brand))
        {
            std::wstringstream notes;
            notes << L"HEIF/HEIC detectado por caja ISO BMFF ftyp";
            if (!brand.empty())
                notes << L" (major brand: " << brand << L")";
            notes << L".\r\n";
            info.containerNotes += notes.str();
        }
        else
        {
            info.containerNotes += L"HEIF/HEIC: extensiÃ³n compatible, pero no se pudo confirmar caja ftyp en lectura rÃ¡pida.\r\n";
        }
    }
}

void MetadataService::ScanJpeg(const std::vector<BYTE>& rawBytes, MetadataInfo& info)
{
    std::wstringstream notes;
    if (rawBytes.size() < 4 || rawBytes[0] != 0xFF || rawBytes[1] != 0xD8)
    {
        notes << L"JPEG: cabecera no estandar.\r\n";
        info.containerNotes += notes.str();
        return;
    }

    size_t pos = 2;
    int exifCount = 0;
    int xmpCount = 0;
    int iccCount = 0;
    int iptcCount = 0;
    int commentCount = 0;

    while (pos + 4 <= rawBytes.size())
    {
        while (pos < rawBytes.size() && rawBytes[pos] != 0xFF) ++pos;
        while (pos < rawBytes.size() && rawBytes[pos] == 0xFF) ++pos;
        if (pos >= rawBytes.size()) break;

        BYTE marker = rawBytes[pos++];
        if (marker == 0xD9 || marker == 0xDA) break;
        if (marker >= 0xD0 && marker <= 0xD7) continue;
        if (pos + 2 > rawBytes.size()) break;

        UINT16 len = ReadBE16(rawBytes.data() + pos);
        if (len < 2 || pos + len > rawBytes.size()) break;

        const BYTE* seg = rawBytes.data() + pos + 2;
        size_t segSize = len - 2;
        std::wstring label = MarkerName(marker);

        if (marker == 0xE0 && StartsWithBytes(seg, segSize, "JFIF\0", 5))
            notes << L"JFIF detectado.\r\n";
        else if (marker == 0xE1 && StartsWithBytes(seg, segSize, "Exif\0\0", 6))
        {
            ++exifCount;
            notes << L"EXIF APP1 detectado (" << segSize << L" bytes).\r\n";
        }
        else if (marker == 0xE1 && StartsWithBytes(seg, segSize, "http://ns.adobe.com/xap/1.0/\0", 29))
        {
            ++xmpCount;
            const std::wstring xmpNamespace = L"http://ns.adobe.com/xap/1.0/";
            std::wstring xmpPreview = BytesToAsciiPreview(seg, segSize, 900);
            if (xmpPreview.rfind(xmpNamespace, 0) == 0)
            {
                xmpPreview.erase(0, xmpNamespace.size());
                while (!xmpPreview.empty() && xmpPreview.front() == L' ')
                    xmpPreview.erase(xmpPreview.begin());
            }

            notes << L"XMP APP1 detectado (" << segSize << L" bytes).\r\n";
            notes << L"  Namespace: http://ns.adobe.com/xap/1.0/\r\n";
            notes << L"  Vista previa:\r\n";

            std::wstring xmpLine;
            size_t xmpColumn = 0;
            for (wchar_t ch : xmpPreview)
            {
                if (ch == L'<' && !xmpLine.empty())
                {
                    notes << L"    " << xmpLine << L"\r\n";
                    xmpLine.clear();
                    xmpColumn = 0;
                }

                xmpLine.push_back(ch);
                ++xmpColumn;

                if (xmpColumn >= 120 && ch == L' ')
                {
                    notes << L"    " << xmpLine << L"\r\n";
                    xmpLine.clear();
                    xmpColumn = 0;
                }
            }

            if (!xmpLine.empty())
                notes << L"    " << xmpLine << L"\r\n";
        }
        else if (marker == 0xE2 && StartsWithBytes(seg, segSize, "ICC_PROFILE\0", 12))
        {
            ++iccCount;
            notes << L"ICC profile APP2 detectado (" << segSize << L" bytes).\r\n";
        }
        else if (marker == 0xED)
        {
            ++iptcCount;
            notes << L"APP13/IPTC detectado (" << segSize << L" bytes).\r\n";
        }
        else if (marker == 0xFE)
        {
            ++commentCount;
            notes << L"Comentario JPEG: " << BytesToAsciiPreview(seg, segSize, 500) << L"\r\n";
        }
        else if (marker >= 0xE0 && marker <= 0xEF)
        {
            notes << label << L" detectado (" << segSize << L" bytes).\r\n";
        }

        pos += len;
    }

    notes << L"Resumen JPEG: EXIF=" << exifCount << L", XMP=" << xmpCount << L", ICC=" << iccCount << L", IPTC=" << iptcCount << L", comentarios=" << commentCount << L".\r\n";
    info.containerNotes += notes.str();
}

void MetadataService::ScanPng(const std::vector<BYTE>& rawBytes, MetadataInfo& info)
{
    static const BYTE sig[8] = { 137,80,78,71,13,10,26,10 };
    std::wstringstream notes;
    if (rawBytes.size() < 8 || memcmp(rawBytes.data(), sig, 8) != 0)
    {
        notes << L"PNG: firma no valida.\r\n";
        info.containerNotes += notes.str();
        return;
    }

    size_t pos = 8;
    while (pos + 12 <= rawBytes.size())
    {
        UINT32 length = ReadBE32(rawBytes.data() + pos);
        std::wstring type = ReadPngType(rawBytes.data() + pos + 4);
        pos += 8;
        if (pos + length + 4 > rawBytes.size()) break;

        notes << L"Chunk PNG " << type << L" (" << length << L" bytes)";
        if ((type == L"tEXt" || type == L"zTXt" || type == L"iTXt") && length > 0)
            notes << L": " << BytesToAsciiPreview(rawBytes.data() + pos, length, 500);
        else if (type == L"iCCP")
            notes << L": perfil ICC comprimido detectado";
        else if (type == L"eXIf")
            notes << L": EXIF en PNG detectado";
        notes << L"\r\n";

        pos += length + 4;
        if (type == L"IEND") break;
    }

    info.containerNotes += notes.str();
}

void MetadataService::ScanBmp(const std::vector<BYTE>& rawBytes, MetadataInfo& info)
{
    std::wstringstream notes;
    if (rawBytes.size() < 54 || rawBytes[0] != 'B' || rawBytes[1] != 'M')
    {
        notes << L"BMP: cabecera no valida.\r\n";
        info.containerNotes += notes.str();
        return;
    }

    UINT32 fileSize = ReadLE32(rawBytes.data() + 2);
    UINT32 pixelOffset = ReadLE32(rawBytes.data() + 10);
    UINT32 dibSize = ReadLE32(rawBytes.data() + 14);
    UINT32 width = ReadLE32(rawBytes.data() + 18);
    UINT32 height = ReadLE32(rawBytes.data() + 22);
    UINT16 bpp = static_cast<UINT16>(rawBytes[28] | (rawBytes[29] << 8));
    notes << L"BMP header: tamano=" << fileSize << L", offset pixeles=" << pixelOffset << L", DIB=" << dibSize << L", ancho=" << width << L", alto=" << height << L", bpp=" << bpp << L".\r\n";
    info.containerNotes += notes.str();
}

std::wstring MetadataService::BytesToAsciiPreview(const BYTE* data, size_t size, size_t maxChars)
{
    std::string out;
    out.reserve(std::min(size, maxChars));
    size_t count = std::min(size, maxChars);
    for (size_t i = 0; i < count; ++i)
    {
        char c = static_cast<char>(data[i]);
        if (c >= 32 && c <= 126) out.push_back(c);
        else if (c == '\0') out.push_back(' ');
    }
    if (size > maxChars) out += " ...";

    int len = MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, nullptr, 0);
    UINT cp = CP_UTF8;
    if (len <= 0)
    {
        cp = CP_ACP;
        len = MultiByteToWideChar(CP_ACP, 0, out.c_str(), -1, nullptr, 0);
    }
    if (len <= 0)
        return L"";
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(cp, 0, out.c_str(), -1, wide.data(), len);
    while (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return CleanLine(wide);
}

std::wstring MetadataService::ReadPngType(const BYTE* p)
{
    wchar_t w[5]{};
    w[0] = static_cast<wchar_t>(p[0]);
    w[1] = static_cast<wchar_t>(p[1]);
    w[2] = static_cast<wchar_t>(p[2]);
    w[3] = static_cast<wchar_t>(p[3]);
    return w;
}

UINT16 MetadataService::ReadBE16(const BYTE* p)
{
    return static_cast<UINT16>((p[0] << 8) | p[1]);
}

UINT32 MetadataService::ReadBE32(const BYTE* p)
{
    return (static_cast<UINT32>(p[0]) << 24) |
           (static_cast<UINT32>(p[1]) << 16) |
           (static_cast<UINT32>(p[2]) << 8) |
           static_cast<UINT32>(p[3]);
}

UINT32 MetadataService::ReadLE32(const BYTE* p)
{
    return static_cast<UINT32>(p[0]) |
           (static_cast<UINT32>(p[1]) << 8) |
           (static_cast<UINT32>(p[2]) << 16) |
           (static_cast<UINT32>(p[3]) << 24);
}






