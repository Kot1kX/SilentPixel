#include "MainWindow.h"
#include "resource.h"
#include <gdiplus.h>
using namespace Gdiplus;
#include "PrivacyGuard.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <shlwapi.h>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cwchar>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace
{
    constexpr int ID_OPEN = 1001;
    constexpr int ID_PREV = 1002;
    constexpr int ID_NEXT = 1003;
    constexpr int ID_ZOOM_IN = 1004;
    constexpr int ID_ZOOM_OUT = 1005;
    constexpr int ID_FIT = 1006;
    constexpr int ID_ACTUAL = 1007;
    constexpr int ID_ROTATE = 1008;
    constexpr int ID_FULL = 1009;
    constexpr int ID_META = 1010;
    constexpr int ID_COPY_SUMMARY = 1011;
    constexpr int ID_COPY_ALL = 1012;
    constexpr int ID_COPY_HASH = 1013;
    constexpr int ID_EXPORT_TXT = 1014;
    constexpr int ID_CLEAN_COPY = 1015;
    constexpr int ID_COPY_GPS = 1016;
    constexpr int ID_OPEN_FOLDER = 1017;
    constexpr int ID_COPY_PATH = 1018;

    constexpr int TOP_BAR = 56;
    constexpr int META_WIDTH = 470;
    constexpr int GAP = 8;
    constexpr int STATUS_H = 24;
    constexpr int BRAND_W = 196;
    constexpr UINT WM_SILENTPIXEL_OPEN_STARTUP = WM_APP + 101;
    constexpr UINT_PTR TIMER_METADATA_REFRESH = 2001;
    constexpr UINT_PTR TIMER_FULL_LOAD = 2002;

    constexpr COLORREF BG = RGB(22, 24, 28);
    constexpr COLORREF VIEW_BG = RGB(14, 15, 18);
    constexpr COLORREF PANEL = RGB(31, 34, 39);
    constexpr COLORREF EDIT_BG = RGB(19, 21, 25);
    constexpr COLORREF TEXT = RGB(232, 235, 239);
    constexpr COLORREF MUTED = RGB(158, 166, 176);
    constexpr COLORREF ACCENT = RGB(86, 156, 214);


    void ApplySilentPixelDarkTheme(HWND control)
    {
        if (!control)
            return;

        // Hint nativo para controles Win32. No dibuja una scrollbar falsa.
        // En Windows 10/11 suele oscurecer la barra de desplazamiento del EDIT.
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }

    void ApplyRuntimePriority()
    {
        SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        // Mantiene el boost dinámico normal de Windows, pero evita entrar en prioridad High.
        SetProcessPriorityBoost(GetCurrentProcess(), FALSE);
        SetThreadPriorityBoost(GetCurrentThread(), FALSE);

#if defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION) && defined(PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
        using SetProcessInformationFn = BOOL (WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto setProcessInformation = kernel32
            ? reinterpret_cast<SetProcessInformationFn>(GetProcAddress(kernel32, "SetProcessInformation"))
            : nullptr;

        if (setProcessInformation)
        {
            PROCESS_POWER_THROTTLING_STATE powerState{};
            powerState.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            powerState.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            powerState.StateMask = 0;
            setProcessInformation(
                GetCurrentProcess(),
                ProcessPowerThrottling,
                &powerState,
                sizeof(powerState));
        }
#endif
    }

    std::wstring Lower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return value;
    }

    void Utf8WriteFile(const std::wstring& path, const std::wstring& text)
    {
        int bytesNeeded = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (bytesNeeded <= 0)
            return;
        std::string utf8(static_cast<size_t>(bytesNeeded), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), bytesNeeded, nullptr, nullptr);
        if (!utf8.empty() && utf8.back() == '\0')
            utf8.pop_back();

        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        BYTE bom[3] = { 0xEF, 0xBB, 0xBF };
        DWORD written = 0;
        WriteFile(file, bom, 3, &written, nullptr);
        if (!utf8.empty())
            WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        CloseHandle(file);
    }
}

MainWindow::MainWindow()
{
}

MainWindow::~MainWindow()
{
    delete cleanExporter_;
    delete loader_;
    if (wicFactory_) wicFactory_->Release();
    if (bgBrush_) DeleteObject(bgBrush_);
    if (panelBrush_) DeleteObject(panelBrush_);
    if (editBrush_) DeleteObject(editBrush_);
    if (uiFont_) DeleteObject(uiFont_);
    if (monoFont_) DeleteObject(monoFont_);
    if (brandFont_) DeleteObject(brandFont_);
    if (creditFont_) DeleteObject(creditFont_);
    if (statusFont_) DeleteObject(statusFont_);
    if (metaFont_) DeleteObject(metaFont_);
}

bool MainWindow::Create(HINSTANCE instance, int showCmd, const std::wstring& startupPath)
{
    instance_ = instance;
    pendingStartupPath_ = startupPath;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory_));
    if (FAILED(hr))
    {
        MessageBoxW(nullptr, L"No se pudo iniciar Windows Imaging Component.", L"SilentPixel", MB_ICONERROR);
        return false;
    }

    loader_ = new ImageLoader(wicFactory_);
    cleanExporter_ = new CleanExportService(wicFactory_);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = L"SilentPixelWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc))
        return false;

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"SilentPixel",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        820,
        nullptr,
        nullptr,
        instance_,
        this);

    if (!hwnd_)
        return false;

    HICON iconLarge = static_cast<HICON>(::LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    HICON iconSmall = static_cast<HICON>(::LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (iconLarge)
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconLarge));
    if (iconSmall)
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));

    ShowWindow(hwnd_, showCmd);
    UpdateWindow(hwnd_);

    if (!pendingStartupPath_.empty())
        PostMessageW(hwnd_, WM_SILENTPIXEL_OPEN_STARTUP, 0, 0);

    return true;
}

HWND MainWindow::Window() const
{
    return hwnd_;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    else
    {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(msg, wParam, lParam);

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


struct MetaAutoScrollState
{
    bool active = false;
    POINT origin{};
    HCURSOR cursor = nullptr;
    bool ownsCursor = false;
    double scrollRemainder = 0.0;
};

constexpr UINT_PTR TIMER_META_AUTOSCROLL = 4701;
constexpr UINT META_AUTOSCROLL_TIMER_MS = 35;
constexpr int META_AUTOSCROLL_DEADZONE_PX = 14;
constexpr double META_AUTOSCROLL_PIXELS_PER_LINE = 360.0;
constexpr double META_AUTOSCROLL_MAX_LINES_PER_TICK = 1.35;

static HCURSOR CreateMetaAutoScrollCursor(bool& ownsCursor)
{
    ownsCursor = false;

    constexpr int W = 32;
    constexpr int H = 32;

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = W;
    bi.bV5Height = -H;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP colorBitmap = CreateDIBSection(
        screenDc,
        reinterpret_cast<BITMAPINFO*>(&bi),
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0);
    ReleaseDC(nullptr, screenDc);

    if (!colorBitmap || !bits)
    {
        if (colorBitmap)
            DeleteObject(colorBitmap);
        return LoadCursorW(nullptr, IDC_SIZEALL);
    }

    ZeroMemory(bits, W * H * 4);

    HDC memDc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBmp = SelectObject(memDc, colorBitmap);

    {
        Gdiplus::Graphics g(memDc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

        Gdiplus::Pen shadow(Gdiplus::Color(115, 0, 0, 0), 3.4f);
        shadow.SetStartCap(Gdiplus::LineCapRound);
        shadow.SetEndCap(Gdiplus::LineCapRound);
        shadow.SetLineJoin(Gdiplus::LineJoinRound);

        Gdiplus::Pen pen(Gdiplus::Color(235, 172, 180, 190), 2.2f);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);

        auto drawArrows = [&](Gdiplus::Pen& p, float offset)
        {
            g.DrawLine(&p, 16.0f + offset, 5.0f + offset, 10.0f + offset, 11.0f + offset);
            g.DrawLine(&p, 16.0f + offset, 5.0f + offset, 22.0f + offset, 11.0f + offset);

            g.DrawLine(&p, 16.0f + offset, 27.0f + offset, 10.0f + offset, 21.0f + offset);
            g.DrawLine(&p, 16.0f + offset, 27.0f + offset, 22.0f + offset, 21.0f + offset);

            g.DrawLine(&p, 5.0f + offset, 16.0f + offset, 11.0f + offset, 10.0f + offset);
            g.DrawLine(&p, 5.0f + offset, 16.0f + offset, 11.0f + offset, 22.0f + offset);

            g.DrawLine(&p, 27.0f + offset, 16.0f + offset, 21.0f + offset, 10.0f + offset);
            g.DrawLine(&p, 27.0f + offset, 16.0f + offset, 21.0f + offset, 22.0f + offset);
        };

        drawArrows(shadow, 1.0f);
        drawArrows(pen, 0.0f);

        Gdiplus::SolidBrush center(Gdiplus::Color(210, 172, 180, 190));
        g.FillEllipse(&center, 14.2f, 14.2f, 3.6f, 3.6f);
    }

    SelectObject(memDc, oldBmp);
    DeleteDC(memDc);

    HBITMAP maskBitmap = CreateBitmap(W, H, 1, 1, nullptr);
    if (!maskBitmap)
    {
        DeleteObject(colorBitmap);
        return LoadCursorW(nullptr, IDC_SIZEALL);
    }

    ICONINFO ii{};
    ii.fIcon = FALSE;
    ii.xHotspot = W / 2;
    ii.yHotspot = H / 2;
    ii.hbmMask = maskBitmap;
    ii.hbmColor = colorBitmap;

    HCURSOR cursor = CreateIconIndirect(&ii);

    DeleteObject(colorBitmap);
    DeleteObject(maskBitmap);

    if (cursor)
    {
        ownsCursor = true;
        return cursor;
    }

    return LoadCursorW(nullptr, IDC_SIZEALL);
}

static void StopMetaAutoScroll(HWND hwnd, MetaAutoScrollState* state)
{
    if (!state)
        return;

    state->active = false;
    state->scrollRemainder = 0.0;
    KillTimer(hwnd, TIMER_META_AUTOSCROLL);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

static void SetMetaAutoScrollCursor(MetaAutoScrollState* state)
{
    if (!state)
        return;

    if (!state->cursor)
        state->cursor = CreateMetaAutoScrollCursor(state->ownsCursor);

    SetCursor(state->cursor ? state->cursor : LoadCursorW(nullptr, IDC_SIZEALL));
}

static int MetaAutoScrollLinesFromDelta(MetaAutoScrollState* state, int delta)
{
    if (!state)
        return 0;

    const int absDelta = delta < 0 ? -delta : delta;
    if (absDelta <= META_AUTOSCROLL_DEADZONE_PX)
    {
        state->scrollRemainder = 0.0;
        return 0;
    }

    const int direction = delta > 0 ? 1 : -1;
    double linesPerTick = static_cast<double>(absDelta - META_AUTOSCROLL_DEADZONE_PX) / META_AUTOSCROLL_PIXELS_PER_LINE;

    if (linesPerTick > META_AUTOSCROLL_MAX_LINES_PER_TICK)
        linesPerTick = META_AUTOSCROLL_MAX_LINES_PER_TICK;

    state->scrollRemainder += static_cast<double>(direction) * linesPerTick;

    int lines = static_cast<int>(state->scrollRemainder);
    if (lines > 2)
        lines = 2;
    if (lines < -2)
        lines = -2;

    state->scrollRemainder -= static_cast<double>(lines);
    return lines;
}

LRESULT CALLBACK MetaEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData)
{
    auto* state = reinterpret_cast<MetaAutoScrollState*>(refData);

    switch (msg)
    {
    case WM_MBUTTONDOWN:
    {
        if (!state)
            break;

        if (state->active)
        {
            StopMetaAutoScroll(hwnd, state);
            return 0;
        }

        state->active = true;
        state->scrollRemainder = 0.0;
        state->origin.x = GET_X_LPARAM(lParam);
        state->origin.y = GET_Y_LPARAM(lParam);
        SetTimer(hwnd, TIMER_META_AUTOSCROLL, META_AUTOSCROLL_TIMER_MS, nullptr);
        SetMetaAutoScrollCursor(state);
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == TIMER_META_AUTOSCROLL && state && state->active)
        {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            const int dy = pt.y - state->origin.y;
            const int lines = MetaAutoScrollLinesFromDelta(state, dy);
            if (lines != 0)
                SendMessageW(hwnd, EM_LINESCROLL, 0, lines);

            SetMetaAutoScrollCursor(state);
            return 0;
        }
        break;
    }

    case WM_SETCURSOR:
        if (state && state->active)
        {
            SetMetaAutoScrollCursor(state);
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MOUSEWHEEL:
    case WM_KILLFOCUS:
        if (state && state->active)
            StopMetaAutoScroll(hwnd, state);
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && state && state->active)
        {
            StopMetaAutoScroll(hwnd, state);
            return 0;
        }
        break;

    case WM_NCDESTROY:
        if (state)
        {
            KillTimer(hwnd, TIMER_META_AUTOSCROLL);
            if (state->cursor && state->ownsCursor)
                DestroyCursor(state->cursor);
            delete state;
        }
        RemoveWindowSubclass(hwnd, MetaEditSubclassProc, subclassId);
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(subclassId);
    auto* self = reinterpret_cast<MainWindow*>(refData);

    const auto repaintButton = [](HWND button)
    {
        if (!button)
            return;
        RedrawWindow(button, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    };

    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_GETDLGCODE:
        if (wParam == VK_LEFT || wParam == VK_RIGHT)
            return DLGC_WANTARROWS;
        break;

    case WM_KEYDOWN:
        if (self && (wParam == VK_LEFT || wParam == VK_RIGHT))
        {
            self->OnKeyDown(wParam);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (self && (hwnd == self->btnPrev_ || hwnd == self->btnNext_))
        {
            SetFocus(hwnd);
            SetCapture(hwnd);
            self->hoverButton_ = hwnd;
            repaintButton(hwnd);
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (self && (hwnd == self->btnPrev_ || hwnd == self->btnNext_))
        {
            if (GetCapture() == hwnd)
                ReleaseCapture();

            POINT pt{};
            pt.x = static_cast<SHORT>(LOWORD(lParam));
            pt.y = static_cast<SHORT>(HIWORD(lParam));

            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (PtInRect(&rc, pt))
            {
                SendMessageW(
                    GetParent(hwnd),
                    WM_COMMAND,
                    MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                    reinterpret_cast<LPARAM>(hwnd));
            }

            repaintButton(hwnd);
            return 0;
        }
        break;

    case WM_MOUSEMOVE:
        if (self && self->hoverButton_ != hwnd)
        {
            HWND previous = self->hoverButton_;
            self->hoverButton_ = hwnd;
            repaintButton(previous);
            repaintButton(hwnd);
        }
        {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        break;

    case WM_MOUSELEAVE:
        if (self && self->hoverButton_ == hwnd)
        {
            self->hoverButton_ = nullptr;
            repaintButton(hwnd);
        }
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, ButtonSubclassProc, 1);
        break;

    default:
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE: OnCreate(); return 0;
    case WM_DESTROY: OnDestroy(); return 0;
    case WM_SIZE: OnSize(); return 0;
    case WM_PAINT: OnPaint(); return 0;
    case WM_COMMAND: OnCommand(LOWORD(wParam)); return 0;
    case WM_SILENTPIXEL_OPEN_STARTUP: OpenStartupPath(); return 0;
    case WM_TIMER:
        if (wParam == TIMER_METADATA_REFRESH)
        {
            KillTimer(hwnd_, TIMER_METADATA_REFRESH);
            if (state_.MetadataVisible())
                UpdateMetadataPanel();
            else
                ApplyMetadataVisibility();
            return 0;
        }
        if (wParam == TIMER_FULL_LOAD)
        {
            KillTimer(hwnd_, TIMER_FULL_LOAD);
            std::wstring path = pendingFullLoadPath_;
            pendingFullLoadPath_.clear();

            if (!path.empty() && state_.HasImage() && state_.Image().path == path && state_.Image().preview)
            {
                if (panning_)
                {
                    pendingFullLoadPath_ = path;
                    SetTimer(hwnd_, TIMER_FULL_LOAD, 900, nullptr);
                    return 0;
                }

                LoadImageFull(path, false, true);
            }

            return 0;
        }
        break;
    case WM_DRAWITEM:
        return OnDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam)) ? TRUE : FALSE;
    case WM_DROPFILES: OnDropFiles(reinterpret_cast<HDROP>(wParam)); return 0;
    case WM_LBUTTONDOWN: OnLeftButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_MOUSEMOVE: OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam); return 0;
    case WM_LBUTTONUP: OnLeftButtonUp(); return 0;
    case WM_CAPTURECHANGED: panning_ = false; return 0;
    case WM_MOUSEWHEEL: OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam)); return 0;
    case WM_KEYDOWN: OnKeyDown(wParam); return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        return reinterpret_cast<LRESULT>(OnCtlColor(reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
    case WM_ERASEBKGND: return 1;
    case WM_GETMINMAXINFO:
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = 920;
        info->ptMinTrackSize.y = 560;
        return 0;
    }
    default:
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void MainWindow::OnCreate()
{
    ApplyRuntimePriority();

    bgBrush_ = CreateSolidBrush(BG);
    panelBrush_ = CreateSolidBrush(PANEL);
    editBrush_ = CreateSolidBrush(EDIT_BG);
    uiFont_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI Variable Text");
    monoFont_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI Variable Text");
    brandFont_ = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI Variable Display");
    creditFont_ = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI Variable Text");
    statusFont_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    metaFont_ = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");

    DragAcceptFiles(hwnd_, TRUE);

    btnOpen_ = MakeButton(hwnd_, ID_OPEN, L"Abrir", instance_, uiFont_);
    btnPrev_ = MakeButton(hwnd_, ID_PREV, L"Anterior", instance_, uiFont_);
    btnNext_ = MakeButton(hwnd_, ID_NEXT, L"Siguiente", instance_, uiFont_);
    btnZoomIn_ = MakeButton(hwnd_, ID_ZOOM_IN, L"+", instance_, uiFont_);
    btnZoomOut_ = MakeButton(hwnd_, ID_ZOOM_OUT, L"-", instance_, uiFont_);
    btnFit_ = MakeButton(hwnd_, ID_FIT, L"Ajustar", instance_, uiFont_);
    btnActual_ = MakeButton(hwnd_, ID_ACTUAL, L"1:1", instance_, uiFont_);
    btnRotate_ = MakeButton(hwnd_, ID_ROTATE, L"Rotar", instance_, uiFont_);
    btnFull_ = MakeButton(hwnd_, ID_FULL, L"Pantalla", instance_, uiFont_);
    btnMeta_ = MakeButton(hwnd_, ID_META, L"Metadatos", instance_, uiFont_);

    metaEdit_ = CreateWindowExW(
        0,
        L"EDIT",
        L"Sin imagen cargada.\r\n\r\nArrastra una imagen compatible aqu\u00ED o pulsa Abrir.\r\n\r\nEl panel mostrar\u00E1 resumen principal, metadatos, hashes y an\u00E1lisis t\u00E9cnico del contenedor.",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        0, 0, 200, 200,
        hwnd_,
        nullptr,
        instance_,
        nullptr);
    SendMessageW(metaEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(metaFont_ ? metaFont_ : uiFont_), TRUE);
    SendMessageW(metaEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(14, 14));
    ApplySilentPixelDarkTheme(metaEdit_);
    SetWindowSubclass(metaEdit_, MetaEditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(new MetaAutoScrollState()));

    btnCopySummary_ = MakeButton(hwnd_, ID_COPY_SUMMARY, L"Copiar resumen", instance_, uiFont_);
    btnCopyAll_ = MakeButton(hwnd_, ID_COPY_ALL, L"Copiar huella", instance_, uiFont_);
    btnCopyPath_ = MakeButton(hwnd_, ID_COPY_PATH, L"Copiar ruta", instance_, uiFont_);
    btnCopyHash_ = MakeButton(hwnd_, ID_COPY_HASH, L"Copiar hash", instance_, uiFont_);
    btnExportTxt_ = MakeButton(hwnd_, ID_EXPORT_TXT, L"Exportar TXT", instance_, uiFont_);
    btnCleanCopy_ = MakeButton(hwnd_, ID_CLEAN_COPY, L"Copia limpia", instance_, uiFont_);
    btnCopyGps_ = MakeButton(hwnd_, ID_COPY_GPS, L"Copiar ubicación", instance_, uiFont_);
    btnOpenFolder_ = MakeButton(hwnd_, ID_OPEN_FOLDER, L"Mostrar archivo", instance_, uiFont_);

    HWND allButtons[] = { btnOpen_, btnPrev_, btnNext_, btnZoomIn_, btnZoomOut_, btnFit_, btnActual_, btnRotate_, btnFull_, btnMeta_, btnCopySummary_, btnCopyAll_, btnCopyPath_, btnCopyHash_, btnExportTxt_, btnCleanCopy_, btnCopyGps_, btnOpenFolder_ };
    for (HWND button : allButtons)
        SubclassInteractiveButton(button);

    statusBar_ = nullptr;

    LayoutControls();
    UpdateStatus();
}

void MainWindow::OnDestroy()
{
    DragAcceptFiles(hwnd_, FALSE);
    hoverButton_ = nullptr;
    panning_ = false;
    KillTimer(hwnd_, TIMER_METADATA_REFRESH);
    KillTimer(hwnd_, TIMER_FULL_LOAD);
    ClearPaintCache();
    state_.Clear();
    PostQuitMessage(0);
}

void MainWindow::OnSize()
{
    if (IsIconic(hwnd_))
    {
        KillTimer(hwnd_, TIMER_FULL_LOAD);
        return;
    }

    LayoutControls();
    RedrawContentArea();
}

void MainWindow::OnPaint()
{
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);

    RECT client{};
    GetClientRect(hwnd_, &client);

    if (client.right <= client.left || client.bottom <= client.top)
    {
        EndPaint(hwnd_, &ps);
        return;
    }

    HDC memDc = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, client.right - client.left, client.bottom - client.top);
    HGDIOBJ oldBmp = SelectObject(memDc, bmp);

    HBRUSH bg = CreateSolidBrush(BG);
    FillRect(memDc, &client, bg);
    DeleteObject(bg);

    RECT imageRc = ImageArea();
    HBRUSH view = CreateSolidBrush(VIEW_BG);
    FillRect(memDc, &imageRc, view);
    DeleteObject(view);

    if (state_.HasImage())
        PaintImage(memDc, imageRc);
    else
        PaintEmpty(memDc, imageRc);

    if (state_.MetadataVisible())
    {
        RECT panel{};
        panel.left = imageRc.right;
        panel.top = TOP_BAR;
        panel.right = client.right;
        panel.bottom = client.bottom - STATUS_H;
        HBRUSH brush = CreateSolidBrush(PANEL);
        FillRect(memDc, &panel, brush);
        DeleteObject(brush);

        HGDIOBJ oldPanelBrush = SelectObject(memDc, GetStockObject(HOLLOW_BRUSH));
const int panelPad = 12;
        const int buttonH = 24;
        const int buttonGap = 8;
        const int buttonBlockGap = panelPad;
        const int bottom = client.bottom - STATUS_H - panelPad;
        const int gridTop = bottom - (buttonH * 4) - (buttonGap * 3);
        RECT editBorder{};
        editBorder.left = panel.left + panelPad - 1;
        editBorder.top = panel.top + panelPad - 1;
        editBorder.right = panel.right - panelPad + 1;
        editBorder.bottom = gridTop - buttonBlockGap + 1;
        HPEN editPen = CreatePen(PS_SOLID, 1, RGB(45, 51, 61));
        HGDIOBJ oldEditPen = SelectObject(memDc, editPen);
        Rectangle(memDc, editBorder.left, editBorder.top, editBorder.right, editBorder.bottom);
        SelectObject(memDc, oldPanelBrush);
        SelectObject(memDc, oldEditPen);
        DeleteObject(editPen);
    }

DrawBrandSignature(memDc);

    RECT statusBg{};
    statusBg.left = 0;
    statusBg.top = std::max(client.top, client.bottom - STATUS_H);
    statusBg.right = client.right;
    statusBg.bottom = client.bottom;
    HBRUSH statusBrush = CreateSolidBrush(BG);
    FillRect(memDc, &statusBg, statusBrush);
    DeleteObject(statusBrush);

    PaintStatusLine(memDc);

    BitBlt(dc, 0, 0, client.right - client.left, client.bottom - client.top, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDc);

    EndPaint(hwnd_, &ps);
}


static bool ShouldDropReportLine(const std::wstring& line)
{
    return line.find(L"Compensaci") != std::wstring::npos ||
           line.find(L"Programa exposici") != std::wstring::npos ||
           line.find(L"exposureBias") != std::wstring::npos ||
           line.find(L"ansi-string") != std::wstring::npos ||
           line.find(L"texto ANSI") != std::wstring::npos;
}

static std::wstring StripReportSeparatorsForClipboard(const std::wstring& text)
{
    std::wstringstream input(text);
    std::wstringstream output;
    std::wstring line;

    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();

        bool separator = !line.empty();
        for (wchar_t ch : line)
        {
            if (ch != L'\u2500' && ch != L'=' && ch != L'-')
            {
                separator = false;
                break;
            }
        }

        if (!separator)
            output << line << L"\r\n";
    }

    return output.str();
}

void MainWindow::OnCommand(WORD id)
{
    switch (id)
    {
    case ID_OPEN: OpenFileDialog(); break;
    case ID_PREV: LoadPrevious(); break;
    case ID_NEXT: LoadNext(); break;
    case ID_ZOOM_IN: ZoomByFactor(1.15); break;
    case ID_ZOOM_OUT: ZoomByFactor(1.0 / 1.15); break;
    case ID_FIT: state_.SetFitToWindow(true); ResetPan(); UpdateStatus(); InvalidateImageAreaOnly(); break;
    case ID_ACTUAL: state_.SetZoom(1.0); ResetPan(); UpdateStatus(); InvalidateImageAreaOnly(); break;
    case ID_ROTATE: state_.RotateClockwise(); ResetPan(); UpdateStatus(); InvalidateImageAreaOnly(); break;
    case ID_FULL: ToggleFullscreen(); break;
    case ID_META:
        state_.ToggleMetadata();
        KillTimer(hwnd_, TIMER_METADATA_REFRESH);

        if (state_.MetadataVisible() && metaEdit_)
        {
            SetWindowTextW(metaEdit_, L"Preparando metadatos...");
            ApplyMetadataTextPadding();
        }

        LayoutControls();

        if (state_.MetadataVisible())
        {
            SetTimer(hwnd_, TIMER_METADATA_REFRESH, (state_.HasImage() && state_.Image().preview) ? 500 : 900, nullptr);
        }

        RedrawContentArea();
        break;
    case ID_COPY_SUMMARY:
        PrivacyGuard::CopyTextToClipboard(hwnd_, BuildSummaryText());
        break;
    case ID_COPY_ALL:
        if (EnsureFullImageLoaded())
        {
            if (state_.MetadataVisible())
                UpdateMetadataPanel();

            PrivacyGuard::CopyTextToClipboard(hwnd_, BuildFingerprintText());
        }
        break;
    case ID_COPY_PATH:
        if (state_.HasImage()) PrivacyGuard::CopyTextToClipboard(hwnd_, state_.Image().path);
        break;
    case ID_COPY_HASH:
        if (EnsureFullImageLoaded())
        {
            if (state_.MetadataVisible())
                UpdateMetadataPanel();

            const auto& img = state_.Image();
            std::wstringstream ss;
            ss << L"SilentPixel - hashes\r\n\r\n";
            ss << L"Archivo: " << img.fileName << L"\r\n\r\n";
            ss << L"SHA-256: " << img.hashes.sha256 << L"\r\n";
            ss << L"SHA-1: " << img.hashes.sha1 << L"\r\n";
            ss << L"MD5: " << img.hashes.md5 << L"\r\n";

            PrivacyGuard::CopyTextToClipboard(hwnd_, ss.str());
        }
        break;
    case ID_EXPORT_TXT: if (EnsureFullImageLoaded()) ExportReportTxt(); break;
    case ID_CLEAN_COPY: if (EnsureFullImageLoaded()) SaveCleanCopy(); break;
    case ID_COPY_GPS:
        if (state_.HasImage() && state_.Image().metadata.HasGps()) PrivacyGuard::CopyTextToClipboard(hwnd_, state_.Image().metadata.GpsText());
        break;
    case ID_OPEN_FOLDER:
        if (state_.HasImage()) PrivacyGuard::OpenContainingFolder(state_.Image().path);
        break;
    default: break;
    }
}

void MainWindow::OnDropFiles(HDROP drop)
{
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> selected;

    for (UINT i = 0; i < count; ++i)
    {
        wchar_t path[MAX_PATH]{};
        if (DragQueryFileW(drop, i, path, MAX_PATH) > 0)
        {
            std::wstring value = path;
            if (ImageLoader::IsSupportedExtension(value))
                selected.push_back(value);
        }
    }

    DragFinish(drop);

    if (selected.empty())
        return;

    std::wstring firstPath = selected.front();

    if (selected.size() == 1)
    {
        navigationFromSelection_ = false;
        LoadImage(firstPath, true);
    }
    else
    {
        navigationFromSelection_ = true;
        state_.SetFolderList(std::move(selected), 0);
        LoadImage(firstPath, false);
    }
}

void MainWindow::OnMouseWheel(short delta)
{
    ZoomByFactor(delta > 0 ? 1.12 : 1.0 / 1.12);
}


void MainWindow::ResetPan()
{
    panX_ = 0;
    panY_ = 0;
    panning_ = false;
}

void MainWindow::ClampPanToImageArea()
{
    if (!state_.HasImage() || state_.DisplayWidth() == 0 || state_.DisplayHeight() == 0)
    {
        ResetPan();
        return;
    }

    RECT rc = ImageArea();
    const int areaW = rc.right - rc.left;
    const int areaH = rc.bottom - rc.top;
    if (areaW <= 0 || areaH <= 0)
    {
        ResetPan();
        return;
    }

    double scale = state_.Zoom();
    if (state_.FitToWindow())
    {
        const double sx = static_cast<double>(areaW) / static_cast<double>(state_.DisplayWidth());
        const double sy = static_cast<double>(areaH) / static_cast<double>(state_.DisplayHeight());
        scale = std::min(sx, sy);
    }

    if (scale <= 0.0)
        scale = 1.0;

    const int imageW = std::max(1, static_cast<int>(state_.DisplayWidth() * scale));
    const int imageH = std::max(1, static_cast<int>(state_.DisplayHeight() * scale));

    const int maxX = std::max(0, (imageW - areaW) / 2);
    const int maxY = std::max(0, (imageH - areaH) / 2);

    panX_ = std::clamp(panX_, -maxX, maxX);
    panY_ = std::clamp(panY_, -maxY, maxY);

    if (maxX == 0)
        panX_ = 0;
    if (maxY == 0)
        panY_ = 0;
}


double MainWindow::CurrentFitZoom() const
{
    if (!state_.HasImage() || state_.DisplayWidth() == 0 || state_.DisplayHeight() == 0)
        return 1.0;

    RECT area = ImageArea();
    const int areaW = area.right - area.left;
    const int areaH = area.bottom - area.top;
    if (areaW <= 0 || areaH <= 0)
        return 1.0;

    const double sx = static_cast<double>(areaW) / static_cast<double>(state_.DisplayWidth());
    const double sy = static_cast<double>(areaH) / static_cast<double>(state_.DisplayHeight());
    const double fit = std::min(sx, sy);

    return fit > 0.0 ? fit : 1.0;
}

void MainWindow::ZoomByFactor(double factor)
{
    if (!state_.HasImage())
        return;

    if (state_.FitToWindow())
        state_.SetZoom(CurrentFitZoom());

    state_.ZoomBy(factor);
    ClampPanToImageArea();
    UpdateStatus();
    InvalidateImageAreaOnly();
}

bool MainWindow::CanPanImage() const
{
    if (!state_.HasImage() || state_.DisplayWidth() == 0 || state_.DisplayHeight() == 0)
        return false;

    RECT rc = ImageArea();
    const int areaW = rc.right - rc.left;
    const int areaH = rc.bottom - rc.top;
    if (areaW <= 0 || areaH <= 0)
        return false;

    double scale = state_.Zoom();
    if (state_.FitToWindow())
    {
        const double sx = static_cast<double>(areaW) / static_cast<double>(state_.DisplayWidth());
        const double sy = static_cast<double>(areaH) / static_cast<double>(state_.DisplayHeight());
        scale = std::min(sx, sy);
    }

    if (scale <= 0.0)
        scale = 1.0;

    const int imageW = std::max(1, static_cast<int>(state_.DisplayWidth() * scale));
    const int imageH = std::max(1, static_cast<int>(state_.DisplayHeight() * scale));
    return imageW > areaW || imageH > areaH;
}

void MainWindow::OnLeftButtonDown(int x, int y)
{
    if (!state_.HasImage() || !CanPanImage())
        return;

    RECT rc = ImageArea();
    POINT pt{ x, y };
    if (!PtInRect(&rc, pt))
        return;

    panning_ = true;
    dragStart_ = pt;
    dragPanStart_.x = panX_;
    dragPanStart_.y = panY_;
    SetCapture(hwnd_);
    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
}

void MainWindow::OnMouseMove(int x, int y, WPARAM keys)
{
    if (!panning_)
        return;

    if ((keys & MK_LBUTTON) == 0)
    {
        OnLeftButtonUp();
        return;
    }

    panX_ = dragPanStart_.x + (x - dragStart_.x);
    panY_ = dragPanStart_.y - (y - dragStart_.y);
    ClampPanToImageArea();

    RECT rc = ImageArea();
    RedrawWindow(hwnd_, &rc, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
}

void MainWindow::OnLeftButtonUp()
{
    if (!panning_)
        return;

    panning_ = false;
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    RECT rc = ImageArea();
    RedrawWindow(hwnd_, &rc, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
}

void MainWindow::OnKeyDown(WPARAM key)
{
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && key == 'O') { OpenFileDialog(); return; }

    switch (key)
    {
    case VK_LEFT: LoadPrevious(); break;
    case VK_RIGHT: LoadNext(); break;
    case VK_OEM_PLUS:
    case VK_ADD: ZoomByFactor(1.15); break;
    case VK_OEM_MINUS:
    case VK_SUBTRACT: ZoomByFactor(1.0 / 1.15); break;
    case '0': state_.SetZoom(1.0); ResetPan(); UpdateStatus(); InvalidateImageAreaOnly(); break;
    case 'F': state_.SetFitToWindow(true); ResetPan(); UpdateStatus(); InvalidateImageAreaOnly(); break;
    case 'R': state_.RotateClockwise(); ResetPan(); UpdateStatus(); InvalidateImageAreaOnly(); break;
    case 'M': state_.ToggleMetadata(); KillTimer(hwnd_, TIMER_METADATA_REFRESH); LayoutControls(); if (state_.MetadataVisible()) UpdateMetadataPanel(); else ApplyMetadataVisibility(); RedrawWholeWindow(); break;
    case VK_F11:
    case VK_RETURN: ToggleFullscreen(); break;
    case VK_ESCAPE: if (fullscreen_) ToggleFullscreen(); break;
    default: break;
    }
}

HBRUSH MainWindow::OnCtlColor(HDC dc, HWND control)
{
    if (control == statusBar_)
    {
        SetTextColor(dc, RGB(176, 184, 194));
        SetBkColor(dc, BG);
        return bgBrush_;
    }
    if (control == metaEdit_)
    {
        SetTextColor(dc, RGB(176, 184, 194));
        SetBkColor(dc, EDIT_BG);
        return editBrush_;
    }

    SetTextColor(dc, TEXT);
    SetBkColor(dc, PANEL);
    return panelBrush_;
}


bool MainWindow::OnDrawItem(DRAWITEMSTRUCT* item)
{
    if (!item || !IsAppButton(item->hwndItem))
        return false;

    const bool navigationButton = item->hwndItem == btnPrev_ || item->hwndItem == btnNext_;
    const bool selectedRaw = (item->itemState & ODS_SELECTED) != 0;
    const bool selected = selectedRaw && !navigationButton;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0;
    const bool hot = (hoverButton_ == item->hwndItem) && !disabled;

    RECT itemRc = item->rcItem;
    const int width = itemRc.right - itemRc.left;
    const int height = itemRc.bottom - itemRc.top;
    if (width <= 0 || height <= 0)
        return true;

    HDC targetDc = item->hDC;
    HDC memDc = CreateCompatibleDC(targetDc);
    HBITMAP memBmp = CreateCompatibleBitmap(targetDc, width, height);
    if (!memDc || !memBmp)
    {
        if (memBmp)
            DeleteObject(memBmp);
        if (memDc)
            DeleteDC(memDc);
        return false;
    }

    HGDIOBJ oldBmp = SelectObject(memDc, memBmp);
    RECT rc{};
    rc.left = 0;
    rc.top = 0;
    rc.right = width;
    rc.bottom = height;

    const bool panelButton =
        item->hwndItem == btnCopySummary_ || item->hwndItem == btnCopyAll_ ||
        item->hwndItem == btnCopyPath_ || item->hwndItem == btnCopyHash_ ||
        item->hwndItem == btnExportTxt_ || item->hwndItem == btnCleanCopy_ ||
        item->hwndItem == btnCopyGps_ || item->hwndItem == btnOpenFolder_;

    const Gdiplus::Color backColor = panelButton
        ? Gdiplus::Color(255, 31, 34, 39)
        : Gdiplus::Color(255, 22, 24, 28);

    Gdiplus::Graphics g(memDc);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);

    Gdiplus::SolidBrush backBrush(backColor);
    Gdiplus::Rect clearRect(0, 0, width, height);
    g.FillRectangle(&backBrush, clearRect);

    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    const auto makeRoundedPath = [](Gdiplus::GraphicsPath& rounded, const Gdiplus::RectF& rect, Gdiplus::REAL radius)
    {
        rounded.Reset();
        const Gdiplus::REAL d = radius * 2.0f;
        rounded.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
        rounded.AddArc(rect.GetRight() - d, rect.Y, d, d, 270.0f, 90.0f);
        rounded.AddArc(rect.GetRight() - d, rect.GetBottom() - d, d, d, 0.0f, 90.0f);
        rounded.AddArc(rect.X, rect.GetBottom() - d, d, d, 90.0f, 90.0f);
        rounded.CloseFigure();
    };

    const Gdiplus::REAL outerRadius = 6.0f;
    const Gdiplus::REAL innerRadius = 5.0f;
    Gdiplus::RectF outerRect(
        0.0f,
        0.0f,
        static_cast<Gdiplus::REAL>(width) - 1.0f,
        static_cast<Gdiplus::REAL>(height) - 1.0f
    );
    Gdiplus::RectF innerRect(
        1.0f,
        1.0f,
        static_cast<Gdiplus::REAL>(width) - 3.0f,
        static_cast<Gdiplus::REAL>(height) - 3.0f
    );

    Gdiplus::Color fill = Gdiplus::Color(255, 27, 31, 37);
    Gdiplus::Color border = Gdiplus::Color(255, 58, 66, 76);
    Gdiplus::Color text = Gdiplus::Color(255, 238, 241, 245);
    Gdiplus::Color glow = Gdiplus::Color(0, 0, 0, 0);
    const bool lit = (hot || selected) && !disabled;

    if (hot)
    {
        fill = Gdiplus::Color(255, 34, 43, 54);
        border = Gdiplus::Color(255, 66, 76, 88);
        glow = Gdiplus::Color(112, 88, 154, 214);
        text = Gdiplus::Color(255, 250, 252, 255);
    }

    if (selected)
    {
        fill = Gdiplus::Color(255, 21, 36, 52);
        border = Gdiplus::Color(255, 62, 72, 84);
        glow = Gdiplus::Color(124, 84, 146, 206);
        text = Gdiplus::Color(255, 250, 252, 255);
    }

    if (disabled)
    {
        fill = Gdiplus::Color(255, 29, 31, 35);
        border = Gdiplus::Color(255, 48, 53, 60);
        text = Gdiplus::Color(255, 125, 132, 140);
    }

    Gdiplus::GraphicsPath outerPath;
    Gdiplus::GraphicsPath innerPath;
    makeRoundedPath(outerPath, outerRect, outerRadius);
    makeRoundedPath(innerPath, innerRect, innerRadius);
    if (lit)
    {
        Gdiplus::RectF glowRect(
            0.7f,
            0.9f,
            static_cast<Gdiplus::REAL>(width) - 2.4f,
            static_cast<Gdiplus::REAL>(height) - 2.8f
        );
        Gdiplus::GraphicsPath glowPath;
        makeRoundedPath(glowPath, glowRect, outerRadius - 0.1f);

        Gdiplus::Pen glowPen(glow, 3.0f);
        glowPen.SetAlignment(Gdiplus::PenAlignmentCenter);
        glowPen.SetLineJoin(Gdiplus::LineJoinRound);
        glowPen.SetStartCap(Gdiplus::LineCapRound);
        glowPen.SetEndCap(Gdiplus::LineCapRound);

        g.DrawPath(&glowPen, &glowPath);
    }

    Gdiplus::SolidBrush borderBrush(border);
    Gdiplus::SolidBrush fillBrush(fill);

    g.FillPath(&borderBrush, &outerPath);
    g.FillPath(&fillBrush, &innerPath);

    wchar_t label[128]{};
    GetWindowTextW(item->hwndItem, label, 128);

    Gdiplus::FontFamily family(L"Segoe UI Variable Text");
    Gdiplus::FontFamily fallback(L"Segoe UI");
    Gdiplus::Font textFont(family.IsAvailable() ? &family : &fallback, 14.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    Gdiplus::SolidBrush textBrush(text);
    Gdiplus::RectF textRect(
        0.0f,
        selected ? 1.0f : 0.0f,
        static_cast<Gdiplus::REAL>(width),
        static_cast<Gdiplus::REAL>(height)
    );

    g.DrawString(label, -1, &textFont, textRect, &fmt, &textBrush);

    BitBlt(targetDc, itemRc.left, itemRc.top, width, height, memDc, 0, 0, SRCCOPY);

    SelectObject(memDc, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDc);

    return true;
}

void MainWindow::DrawBrandSignature(HDC dc) const
{
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (client.right < 1080)
        return;

    HICON brandIcon = static_cast<HICON>(::LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        32,
        32,
        LR_DEFAULTCOLOR));

    if (brandIcon)
    {
        DrawIconEx(dc, GAP + 2, 15, brandIcon, 32, 32, 0, nullptr, DI_NORMAL);
        DestroyIcon(brandIcon);
    }

    const int textX = GAP + 40;
    const int titleY = 8;
    const int creditY = 35;

    const int oldBkMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldTextColor = GetTextColor(dc);

    HFONT titleFont = CreateFontW(
        -24, 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    HFONT creditFont = CreateFontW(
        -12, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    HFONT oldFont = nullptr;
    if (titleFont)
        oldFont = static_cast<HFONT>(SelectObject(dc, titleFont));

    SetTextColor(dc, RGB(232, 235, 239));
    TextOutW(dc, textX, titleY, L"Silent", 6);

    SIZE silentSize{};
    GetTextExtentPoint32W(dc, L"Silent", 6, &silentSize);

    SetTextColor(dc, RGB(86, 156, 214));
    TextOutW(dc, textX + silentSize.cx + 1, titleY, L"Pixel", 5);

    if (creditFont)
        SelectObject(dc, creditFont);

    SetTextColor(dc, RGB(148, 157, 168));
    TextOutW(dc, textX + 1, creditY, L"Creado por Kot1kX", 17);

    if (oldFont)
        SelectObject(dc, oldFont);

    if (titleFont)
        DeleteObject(titleFont);
    if (creditFont)
        DeleteObject(creditFont);

    SetTextColor(dc, oldTextColor);
    SetBkMode(dc, oldBkMode);
}

void MainWindow::OpenStartupPath()
{
    if (pendingStartupPath_.empty())
        return;

    std::wstring path = pendingStartupPath_;
    pendingStartupPath_.clear();

    if (!PathFileExistsW(path.c_str()) || !ImageLoader::IsSupportedExtension(path))
    {
        MessageBoxW(hwnd_, L"No se pudo abrir la imagen. El formato puede no estar soportado por los codecs instalados en Windows.", L"SilentPixel", MB_ICONERROR);
        return;
    }

    LoadImage(path, true);
}

void MainWindow::OpenFileDialog()
{
    std::vector<wchar_t> buffer(65536, L'\0');

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Im\u00E1genes (*.jpg;*.jpeg;*.jpe;*.jfif;*.jfi;*.png;*.bmp;*.gif;*.tif;*.tiff;*.ico;*.webp;*.heic;*.heif)\0*.jpg;*.jpeg;*.jpe;*.jfif;*.jfi;*.png;*.bmp;*.gif;*.tif;*.tiff;*.ico;*.webp;*.heic;*.heif\0Todos los archivos\0*.*\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    ofn.lpstrTitle = L"Abrir una o varias im\u00E1genes";

    if (!GetOpenFileNameW(&ofn))
        return;

    std::wstring first = buffer.data();
    wchar_t* cursor = buffer.data() + first.size() + 1;

    std::vector<std::wstring> selected;

    if (*cursor == L'\0')
    {
        if (ImageLoader::IsSupportedExtension(first))
            selected.push_back(first);
    }
    else
    {
        std::filesystem::path folder(first);
        while (*cursor != L'\0')
        {
            std::wstring name = cursor;
            std::wstring path = (folder / name).wstring();
            if (ImageLoader::IsSupportedExtension(path))
                selected.push_back(path);
            cursor += name.size() + 1;
        }
    }

    if (selected.empty())
        return;

    std::wstring firstPath = selected.front();

    if (selected.size() == 1)
    {
        navigationFromSelection_ = false;
        LoadImage(firstPath, true);
    }
    else
    {
        navigationFromSelection_ = true;
        state_.SetFolderList(std::move(selected), 0);
        LoadImage(firstPath, false);
    }
}

bool MainWindow::LoadImage(const std::wstring& path, bool rebuildFolder, bool quietErrors)
{
    ApplyRuntimePriority();
    SetStatusText(L"Cargando vista rápida...");

    LoadedImage image;
    std::wstring error;
    if (!loader_->LoadPreview(path, image, error, 1920))
    {
        if (!quietErrors)
            MessageBoxW(hwnd_, error.c_str(), L"SilentPixel", MB_ICONERROR);

        SetStatusText(L"Omitida imagen no compatible o sin codec WIC.");
        return false;
    }

    const bool isPreview = image.preview;

    state_.SetImage(std::move(image));
    ClearPaintCache();
    ResetPan();
    if (rebuildFolder)
        RebuildFolderList(path);

    UpdateTitle();

    KillTimer(hwnd_, TIMER_FULL_LOAD);
    pendingFullLoadPath_.clear();

    KillTimer(hwnd_, TIMER_METADATA_REFRESH);
    if (state_.MetadataVisible())
    {
        if (metaEdit_)
        {
            SetWindowTextW(metaEdit_, isPreview ? L"Preparando vista rápida..." : L"Actualizando metadatos...");
            ApplyMetadataTextPadding();
        }

        SetTimer(hwnd_, TIMER_METADATA_REFRESH, isPreview ? 500 : 900, nullptr);
    }
    else
    {
        ApplyMetadataVisibility();
    }
    UpdateStatus();

    InvalidateImageAreaOnly();
    return true;
}

bool MainWindow::LoadImageFull(const std::wstring& path, bool rebuildFolder, bool quietErrors)
{
    ApplyRuntimePriority();

    const bool preserveView =
        state_.HasImage() &&
        state_.Image().path == path &&
        state_.Image().preview;

    const bool oldFitToWindow = state_.FitToWindow();
    const double oldFitZoom = preserveView ? CurrentFitZoom() : 1.0;
    const double oldZoom = state_.Zoom();
    const int oldRotation = state_.Rotation();
    const int oldPanX = panX_;
    const int oldPanY = panY_;
    const bool oldPanning = panning_;
    const POINT oldDragStart = dragStart_;
    const POINT oldDragPanStart = dragPanStart_;

    LoadedImage image;
    std::wstring error;
    if (!loader_->Load(path, image, error))
    {
        if (!quietErrors)
            MessageBoxW(hwnd_, error.c_str(), L"SilentPixel", MB_ICONERROR);

        SetStatusText(L"No se pudo cargar la imagen completa.");
        return false;
    }

    state_.SetImage(std::move(image));
    ClearPaintCache();
    BuildPaintCacheIfNeeded();

    if (preserveView)
    {
        const int normalizedRotation = ((oldRotation % 360) + 360) % 360;
        const int rotationSteps = normalizedRotation / 90;
        for (int i = 0; i < rotationSteps; ++i)
            state_.RotateClockwise();

        if (oldFitToWindow)
        {
            state_.SetFitToWindow(true);
        }
        else
        {
            const double zoomRatio = oldFitZoom > 0.0 ? (oldZoom / oldFitZoom) : 1.0;
            const double newFitZoom = CurrentFitZoom();
            state_.SetZoom(newFitZoom * zoomRatio);
        }

        panX_ = oldPanX;
        panY_ = oldPanY;
        panning_ = oldPanning;
        dragStart_ = oldDragStart;
        dragPanStart_ = oldDragPanStart;
        ClampPanToImageArea();
    }
    else
    {
        ResetPan();
    }

    if (rebuildFolder)
        RebuildFolderList(path);

    pendingFullLoadPath_.clear();
    KillTimer(hwnd_, TIMER_FULL_LOAD);

    UpdateTitle();
    KillTimer(hwnd_, TIMER_METADATA_REFRESH);
    if (state_.MetadataVisible())
        SetTimer(hwnd_, TIMER_METADATA_REFRESH, 350, nullptr);
    else
        ApplyMetadataVisibility();

    UpdateStatus();
    InvalidateImageAreaOnly();
    return true;
}

bool MainWindow::EnsureFullImageLoaded()
{
    if (!state_.HasImage())
        return false;

    if (!state_.Image().preview)
        return true;

    const std::wstring path = state_.Image().path;
    SetStatusText(L"Cargando imagen completa...");
    return LoadImageFull(path, false, false);
}

bool MainWindow::CanNavigateNow() const
{
    const ULONGLONG now = GetTickCount64();
    return now >= navigationCooldownUntil_;
}

void MainWindow::MarkNavigationHandled()
{
    navigationCooldownUntil_ = GetTickCount64() + 150;
}

void MainWindow::LoadNext()
{
    if (!CanNavigateNow())
        return;

    if (!state_.HasImage())
        return;

    if (state_.NavigationCount() <= 1)
        RebuildFolderList(state_.Image().path);

    if (state_.NavigationCount() <= 1)
    {
        MarkNavigationHandled();
        SetStatusText(navigationFromSelection_ ? L"Fin de im\u00E1genes seleccionadas." : L"No hay m\u00E1s im\u00E1genes.");
        return;
    }

    while (true)
    {
        std::wstring path = state_.MoveNext();
        if (path.empty())
            break;

        if (LoadImage(path, false, true))
        {
            MarkNavigationHandled();
            return;
        }
    }

    MarkNavigationHandled();
    SetStatusText(navigationFromSelection_ ? L"Fin de im\u00E1genes seleccionadas." : L"No hay m\u00E1s im\u00E1genes.");
}

void MainWindow::LoadPrevious()
{
    if (!CanNavigateNow())
        return;

    if (!state_.HasImage())
        return;

    if (state_.NavigationCount() <= 1)
        RebuildFolderList(state_.Image().path);

    if (state_.NavigationCount() <= 1)
    {
        MarkNavigationHandled();
        SetStatusText(navigationFromSelection_ ? L"Fin de im\u00E1genes seleccionadas." : L"No hay m\u00E1s im\u00E1genes.");
        return;
    }

    while (true)
    {
        std::wstring path = state_.MovePrevious();
        if (path.empty())
            break;

        if (LoadImage(path, false, true))
        {
            MarkNavigationHandled();
            return;
        }
    }

    MarkNavigationHandled();
    SetStatusText(navigationFromSelection_ ? L"Fin de im\u00E1genes seleccionadas." : L"No hay im\u00E1genes anteriores.");
}

void MainWindow::RebuildFolderList(const std::wstring& path)
{
    navigationFromSelection_ = false;
    std::vector<std::wstring> files;
    std::wstring dir = DirectoryFromPath(path);
    if (dir.empty())
    {
        state_.SetFolderList({}, 0);
        return;
    }

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;
            std::wstring p = entry.path().wstring();
            if (ImageLoader::IsSupportedExtension(p))
                files.push_back(p);
        }
    }
    catch (...)
    {
        state_.SetFolderList({}, 0);
        return;
    }

    std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    size_t index = 0;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (_wcsicmp(files[i].c_str(), path.c_str()) == 0)
        {
            index = i;
            break;
        }
    }
    state_.SetFolderList(std::move(files), index);
}

void MainWindow::UpdateTitle()
{
    if (state_.HasImage())
        SetWindowTextW(hwnd_, (L"SilentPixel - " + state_.Image().fileName).c_str());
    else
        SetWindowTextW(hwnd_, L"SilentPixel");
}

void MainWindow::ApplyMetadataVisibility()
{
    const bool showMeta = state_.MetadataVisible();

    HWND metaControls[] = {
        metaEdit_, btnCopySummary_, btnCopyAll_, btnCopyPath_, btnCopyHash_,
        btnExportTxt_, btnCleanCopy_, btnCopyGps_, btnOpenFolder_
    };

    bool hoverWasMetadataControl = false;
    for (HWND control : metaControls)
    {
        if (!control)
            continue;

        if (hoverButton_ == control)
            hoverWasMetadataControl = true;

        EnableWindow(control, showMeta ? TRUE : FALSE);

        const BOOL visibleNow = IsWindowVisible(control);
        if ((showMeta && !visibleNow) || (!showMeta && visibleNow))
        {
            SetWindowPos(
                control,
                nullptr,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                (showMeta ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        }
    }

    if (!showMeta)
    {
        KillTimer(hwnd_, TIMER_METADATA_REFRESH);
        if (hoverWasMetadataControl)
            hoverButton_ = nullptr;
    }

    if (hwnd_)
        RedrawWholeWindow();

}
void MainWindow::ApplyMetadataTextPadding()
{
    if (!metaEdit_)
        return;

    RECT rc{};
    GetClientRect(metaEdit_, &rc);

    if (rc.right <= rc.left || rc.bottom <= rc.top)
        return;

    RECT textRect{};
    textRect.left = 8;
    textRect.top = 10;

    LONG textRight = rc.right - 22;
    if (textRight < 16)
        textRight = 16;

    LONG textBottom = rc.bottom - 14;
    const LONG minTextBottom = textRect.top + 16;
    if (textBottom < minTextBottom)
        textBottom = minTextBottom;

    textRect.right = textRight;
    textRect.bottom = textBottom;

    SendMessageW(metaEdit_, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&textRect));
}

void MainWindow::UpdateMetadataPanel()
{
    if (!metaEdit_ || !state_.MetadataVisible())
    {
        ApplyMetadataVisibility();
        return;
    }

    std::wstring panelText = state_.HasImage()
        ? StripReportSeparatorsForClipboard(BuildPanelReportText())
        : BuildPanelReportText();

    SendMessageW(metaEdit_, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(metaEdit_, panelText.c_str());
    ApplyMetadataTextPadding();
    SendMessageW(metaEdit_, EM_SETSEL, 0, 0);
    SendMessageW(metaEdit_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(metaEdit_, nullptr, FALSE);
}

RECT MainWindow::StatusArea() const
{
    RECT client{};
    if (hwnd_)
        GetClientRect(hwnd_, &client);

    RECT rc{};
    rc.left = GAP;
    rc.top = std::max(client.top, client.bottom - STATUS_H + 1);
    rc.right = std::max(rc.left, client.right);
    rc.bottom = std::max(rc.top, client.bottom - 1);
    return rc;
}

void MainWindow::SetStatusText(const std::wstring& text)
{
    if (statusText_ == text)
        return;

    statusText_ = text;

    if (!hwnd_)
        return;

    RECT rc = StatusArea();
    InvalidateRect(hwnd_, &rc, FALSE);
}

void MainWindow::PaintStatusLine(HDC dc) const
{
    if (statusText_.empty())
        return;

    RECT rc = StatusArea();
    HGDIOBJ oldFont = nullptr;
    if (statusFont_)
        oldFont = SelectObject(dc, statusFont_);
    else if (uiFont_)
        oldFont = SelectObject(dc, uiFont_);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(176, 184, 194));

    RECT textRc = rc;
    textRc.left = rc.left;
    textRc.right -= 8;
    DrawTextW(dc, statusText_.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (oldFont)
        SelectObject(dc, oldFont);
}

void MainWindow::UpdateStatus()
{
    std::wstring text;
    if (!state_.HasImage())
    {
        text = L"Arrastra una imagen compatible o pulsa Abrir";
    }
    else
    {
        const auto& img = state_.Image();
        const UINT reportW = img.originalWidth ? img.originalWidth : img.width;
        const UINT reportH = img.originalHeight ? img.originalHeight : img.height;

        int userZoomPercent = 0;

        if (!state_.FitToWindow())
        {
            const double fitZoom = CurrentFitZoom();
            const double currentZoom = state_.Zoom();

            if (fitZoom > 0.0)
            {
                if (currentZoom >= fitZoom)
                {
                    userZoomPercent = static_cast<int>((currentZoom * 100.0) + 0.5);
                }
                else
                {
                    double shrinkPercent = (1.0 - (currentZoom / fitZoom)) * 100.0;
                    if (shrinkPercent < 0.0)
                        shrinkPercent = 0.0;
                    if (shrinkPercent > 100.0)
                        shrinkPercent = 100.0;

                    userZoomPercent = -static_cast<int>(shrinkPercent + 0.5);
                }
            }
        }

        std::wstringstream ss;
        ss << img.fileName
           << L"  ·  " << reportW << L"×" << reportH << L" px"
           << L"  ·  Ampliación " << userZoomPercent << L"%"
           << L"  ·  Rotación " << state_.Rotation() << L"°"
           << L"  ·  " << FormatFileSize(img.fileSize)
;

        text = ss.str();
    }

    SetStatusText(text);
}

void MainWindow::ToggleFullscreen()
{
    fullscreen_ = !fullscreen_;
    if (fullscreen_)
    {
        windowedStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        windowedExStyle_ = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        GetWindowRect(hwnd_, &windowedRect_);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, windowedStyle_ & ~WS_OVERLAPPEDWINDOW);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, windowedExStyle_ & ~WS_EX_WINDOWEDGE);
        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(monitor, &mi);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
    }
    else
    {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, windowedStyle_);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, windowedExStyle_);
        SetWindowPos(hwnd_, nullptr, windowedRect_.left, windowedRect_.top, windowedRect_.right - windowedRect_.left, windowedRect_.bottom - windowedRect_.top, SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

void MainWindow::LayoutControls()
{
    RECT client{};
    GetClientRect(hwnd_, &client);

    int x = (client.right >= 1080) ? BRAND_W : GAP;
    const int y = 16;
    const int h = 24;
    const int toolbarGap = 8;
    struct Item { HWND hwnd; int w; } items[] = {
        { btnOpen_, 66 }, { btnPrev_, 76 }, { btnNext_, 78 }, { btnZoomIn_, 38 }, { btnZoomOut_, 38 },
        { btnFit_, 70 }, { btnActual_, 48 }, { btnRotate_, 64 }, { btnFull_, 78 }, { btnMeta_, 98 }
    };

    HDWP hdwp = BeginDeferWindowPos(20);

    auto deferMove = [&](HWND hwnd, int mx, int my, int mw, int mh)
    {
        if (!hwnd)
            return;

        if (hdwp)
        {
            HDWP next = DeferWindowPos(
                hdwp,
                hwnd,
                nullptr,
                mx,
                my,
                mw,
                mh,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            if (next)
            {
                hdwp = next;
                return;
            }
        }

        MoveWindow(hwnd, mx, my, mw, mh, FALSE);
    };

    for (auto& item : items)
    {
        if (item.hwnd)
        {
            deferMove(item.hwnd, x, y, item.w, h);
            x += item.w + toolbarGap;
        }
    }

    const bool showMeta = state_.MetadataVisible();

    if (showMeta)
    {
        RECT rc = ImageArea();
        int px = rc.right;
        int py = TOP_BAR;
        int pw = client.right - px;
        int bottom = client.bottom - STATUS_H;
        int panelPad = 12;
        int buttonH = 24;
        int buttonGap = 8;
        int buttonBlockGap = panelPad;
        bottom -= panelPad;
        int gridTop = bottom - (buttonH * 4) - (buttonGap * 3);
        const int editW = pw - panelPad * 2;
        const int editH = gridTop - py - panelPad - buttonBlockGap;

        deferMove(metaEdit_, px + panelPad, py + panelPad, editW, editH);

        int bx1 = px + panelPad;
        int bx2 = px + panelPad + (pw - panelPad * 2 - buttonGap) / 2 + buttonGap;
        int bw = (pw - panelPad * 2 - buttonGap) / 2;
        deferMove(btnCopySummary_, bx1, gridTop, bw, buttonH);
        deferMove(btnCopyAll_, bx2, gridTop, bw, buttonH);
        deferMove(btnCopyPath_, bx1, gridTop + (buttonH + buttonGap), bw, buttonH);
        deferMove(btnCopyHash_, bx2, gridTop + (buttonH + buttonGap), bw, buttonH);
        deferMove(btnExportTxt_, bx1, gridTop + (buttonH + buttonGap) * 2, bw, buttonH);
        deferMove(btnCleanCopy_, bx2, gridTop + (buttonH + buttonGap) * 2, bw, buttonH);
        deferMove(btnCopyGps_, bx1, gridTop + (buttonH + buttonGap) * 3, bw, buttonH);
        deferMove(btnOpenFolder_, bx2, gridTop + (buttonH + buttonGap) * 3, bw, buttonH);
    }

    if (statusBar_)
    {
        deferMove(statusBar_, GAP, client.bottom - STATUS_H + 2, client.right - GAP * 2, STATUS_H - 3);
    }

    if (hdwp)
        EndDeferWindowPos(hdwp);

    if (showMeta)
        ApplyMetadataTextPadding();

    ApplyMetadataVisibility();
}

void MainWindow::RedrawWholeWindow()
{
    if (!hwnd_)
        return;

    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void MainWindow::RedrawContentArea()
{
    if (!hwnd_)
        return;

    RECT client{};
    GetClientRect(hwnd_, &client);

    RECT rc{};
    rc.left = client.left;
    rc.top = TOP_BAR;
    rc.right = client.right;
    rc.bottom = client.bottom - STATUS_H;

    if (rc.right < rc.left) rc.right = rc.left;
    if (rc.bottom < rc.top) rc.bottom = rc.top;

    RedrawWindow(hwnd_, &rc, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void MainWindow::ClearPaintCache()
{
    PrivacyGuard::ZeroVector(paintCachePixels_);
    paintCacheWidth_ = 0;
    paintCacheHeight_ = 0;
    paintCacheStride_ = 0;
    paintCachePath_.clear();
}

void MainWindow::BuildPaintCacheIfNeeded()
{
    ClearPaintCache();

    if (!state_.HasImage())
        return;

    const LoadedImage& img = state_.Image();

    if (img.preview || img.pixelsBGRA.empty() || img.width == 0 || img.height == 0)
        return;

    if (state_.Rotation() != 0)
        return;

    const unsigned long long pixelCount = static_cast<unsigned long long>(img.width) * static_cast<unsigned long long>(img.height);
    const UINT maxDim = (img.width > img.height) ? img.width : img.height;

    if (pixelCount < 16000000ull && maxDim < 6000)
        return;

    const UINT targetMaxEdge = 2400;
    if (maxDim <= targetMaxEdge)
        return;

    UINT targetW = static_cast<UINT>((static_cast<unsigned long long>(img.width) * targetMaxEdge + maxDim / 2) / maxDim);
    UINT targetH = static_cast<UINT>((static_cast<unsigned long long>(img.height) * targetMaxEdge + maxDim / 2) / maxDim);
    if (targetW < 1) targetW = 1;
    if (targetH < 1) targetH = 1;

    std::vector<BYTE> cache;
    const UINT stride = targetW * 4;
    cache.resize(static_cast<size_t>(stride) * targetH);

    for (UINT y = 0; y < targetH; ++y)
    {
        const UINT sy = targetH <= 1 ? 0 : static_cast<UINT>((static_cast<unsigned long long>(y) * (img.height - 1)) / (targetH - 1));
        const BYTE* srcRow = img.pixelsBGRA.data() + static_cast<size_t>(sy) * img.stride;
        BYTE* dstRow = cache.data() + static_cast<size_t>(y) * stride;

        for (UINT x = 0; x < targetW; ++x)
        {
            const UINT sx = targetW <= 1 ? 0 : static_cast<UINT>((static_cast<unsigned long long>(x) * (img.width - 1)) / (targetW - 1));
            const BYTE* src = srcRow + static_cast<size_t>(sx) * 4;
            BYTE* dst = dstRow + static_cast<size_t>(x) * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }

    paintCachePixels_ = std::move(cache);
    paintCacheWidth_ = targetW;
    paintCacheHeight_ = targetH;
    paintCacheStride_ = stride;
    paintCachePath_ = img.path;
}

void MainWindow::InvalidateImageAreaOnly()
{
    RECT rc = ImageArea();
    InvalidateRect(hwnd_, &rc, FALSE);
}

std::wstring MainWindow::BuildSummaryText() const
{
    if (!state_.HasImage())
        return L"SilentPixel: sin imagen cargada.";

    const auto& img = state_.Image();
    const auto& meta = img.metadata;
    const UINT reportW = img.originalWidth ? img.originalWidth : img.width;
    const UINT reportH = img.originalHeight ? img.originalHeight : img.height;

    auto cleanOrNA = [](const std::wstring& value) -> std::wstring
    {
        return value.empty() ? L"No disponible" : value;
    };

    std::wstringstream ss;
    ss << L"SilentPixel - resumen\r\n";
    ss << L"Archivo: " << img.fileName << L"\r\n";
    ss << L"Resoluci\u00F3n: " << reportW << L"x" << reportH << L" px\r\n";
    ss << L"Formato: " << cleanOrNA(meta.formatName) << L"\r\n";
    ss << L"Tama\u00F1o: " << FormatFileSize(img.fileSize) << L"\r\n\r\n";

    ss << L"Resumen principal\r\n";
    ss << L"Dispositivo: " << (meta.make.empty() && meta.model.empty() ? L"No disponible" : (meta.make.empty() ? meta.model : (meta.model.empty() ? meta.make : meta.make + L" " + meta.model))) << L"\r\n";
    ss << L"Software: " << cleanOrNA(meta.software) << L"\r\n";
    ss << L"Fecha original: " << cleanOrNA(meta.dateOriginal) << L"\r\n";

    if (meta.HasShootingData())
    {
        ss << L"Disparo:\r\n";
        if (!meta.isoSpeed.empty()) ss << L"  ISO: " << meta.isoSpeed << L"\r\n";
        if (!meta.fNumber.empty()) ss << L"  Diafragma: " << meta.fNumber << L"\r\n";
        if (!meta.exposureTime.empty()) ss << L"  Velocidad: " << meta.exposureTime << L"\r\n";
        if (!meta.focalLength.empty()) ss << L"  Focal: " << meta.focalLength << L"\r\n";
        if (!meta.lensModel.empty()) ss << L"  Lente: " << meta.lensModel << L"\r\n";
    }

    ss << L"Coordenadas: " << (meta.HasGps() ? meta.GpsText() : L"No disponible") << L"\r\n";

    if (meta.HasGps())
    {
        ss << L"Aviso: contiene ubicación. SilentPixel no abre mapas ni llama APIs externas.\r\n";
    }

    ss << L"\r\nHashes\r\n";
    ss << L"SHA-256: " << img.hashes.sha256 << L"\r\n";
    ss << L"SHA-1: " << img.hashes.sha1 << L"\r\n";
    ss << L"MD5: " << img.hashes.md5 << L"\r\n";

    return ss.str();
}




static BYTE PixelLumaAt(const LoadedImage& img, UINT x, UINT y)
{
    if (img.pixelsBGRA.empty() || x >= img.width || y >= img.height)
        return 0;

    const size_t offset = static_cast<size_t>(y) * img.stride + static_cast<size_t>(x) * 4;
    if (offset + 2 >= img.pixelsBGRA.size())
        return 0;

    const BYTE b = img.pixelsBGRA[offset + 0];
    const BYTE g = img.pixelsBGRA[offset + 1];
    const BYTE r = img.pixelsBGRA[offset + 2];

    return static_cast<BYTE>((static_cast<UINT>(r) * 299 + static_cast<UINT>(g) * 587 + static_cast<UINT>(b) * 114) / 1000);
}

static std::wstring Hex64(unsigned long long value)
{
    std::wstringstream ss;
    ss << std::hex << std::nouppercase << std::setw(16) << std::setfill(L'0') << value;
    return ss.str();
}

static std::wstring BuildAverageHash64(const LoadedImage& img)
{
    if (img.width == 0 || img.height == 0 || img.pixelsBGRA.empty())
        return L"0000000000000000";

    BYTE samples[64]{};
    UINT sum = 0;

    for (UINT y = 0; y < 8; ++y)
    {
        for (UINT x = 0; x < 8; ++x)
        {
            const UINT sx = img.width == 1 ? 0 : static_cast<UINT>((static_cast<unsigned long long>(x) * (img.width - 1)) / 7);
            const UINT sy = img.height == 1 ? 0 : static_cast<UINT>((static_cast<unsigned long long>(y) * (img.height - 1)) / 7);
            const BYTE v = PixelLumaAt(img, sx, sy);
            samples[y * 8 + x] = v;
            sum += v;
        }
    }

    const UINT avg = sum / 64;
    unsigned long long hash = 0;
    for (UINT i = 0; i < 64; ++i)
    {
        if (samples[i] >= avg)
            hash |= (1ull << i);
    }

    return Hex64(hash);
}

static std::wstring BuildDifferenceHash64(const LoadedImage& img)
{
    if (img.width == 0 || img.height == 0 || img.pixelsBGRA.empty())
        return L"0000000000000000";

    unsigned long long hash = 0;
    UINT bit = 0;

    for (UINT y = 0; y < 8; ++y)
    {
        for (UINT x = 0; x < 8; ++x)
        {
            const UINT sx1 = img.width == 1 ? 0 : static_cast<UINT>((static_cast<unsigned long long>(x) * (img.width - 1)) / 8);
            const UINT sx2 = img.width == 1 ? 0 : static_cast<UINT>((static_cast<unsigned long long>(x + 1) * (img.width - 1)) / 8);
            const UINT sy = img.height == 1 ? 0 : static_cast<UINT>((static_cast<unsigned long long>(y) * (img.height - 1)) / 7);

            if (PixelLumaAt(img, sx1, sy) > PixelLumaAt(img, sx2, sy))
                hash |= (1ull << bit);

            ++bit;
        }
    }

    return Hex64(hash);
}

static std::wstring BuildDecodedPixelHash64(const LoadedImage& img)
{
    unsigned long long hash = 1469598103934665603ull;

    auto feedByte = [&](BYTE value)
    {
        hash ^= static_cast<unsigned long long>(value);
        hash *= 1099511628211ull;
    };

    auto feedUInt = [&](UINT value)
    {
        for (int i = 0; i < 4; ++i)
            feedByte(static_cast<BYTE>((value >> (i * 8)) & 0xFF));
    };

    feedUInt(img.width);
    feedUInt(img.height);
    feedUInt(img.stride);

    for (BYTE value : img.pixelsBGRA)
        feedByte(value);

    return Hex64(hash);
}

static bool TextContainsInsensitive(std::wstring haystack, std::wstring needle)
{
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return haystack.find(needle) != std::wstring::npos;
}

static int ReadContainerSummaryCount(const std::wstring& notes, const std::wstring& label)
{
    const std::wstring needle = label + L"=";
    size_t pos = notes.find(needle);
    if (pos == std::wstring::npos)
        return -1;

    pos += needle.size();

    int value = 0;
    bool foundDigit = false;
    while (pos < notes.size() && notes[pos] >= L'0' && notes[pos] <= L'9')
    {
        foundDigit = true;
        value = (value * 10) + static_cast<int>(notes[pos] - L'0');
        ++pos;
    }

    return foundDigit ? value : -1;
}

static bool HasContainerMarkerExact(const MetadataInfo& meta, const std::wstring& label)
{
    const int count = ReadContainerSummaryCount(meta.containerNotes, label);
    if (count >= 0)
        return count > 0;

    if (label == L"EXIF")
    {
        if (TextContainsInsensitive(meta.containerNotes, L"EXIF APP1 detectado") ||
            TextContainsInsensitive(meta.containerNotes, L"libheif EXIF: parse TIFF interno aplicado"))
        {
            return true;
        }

        if (!meta.make.empty() ||
            !meta.model.empty() ||
            !meta.software.empty() ||
            !meta.dateOriginal.empty() ||
            !meta.orientation.empty() ||
            meta.HasShootingData() ||
            meta.HasGps())
        {
            return true;
        }

        for (const auto& entry : meta.entries)
        {
            if (TextContainsInsensitive(entry.section, L"/app1/") ||
                TextContainsInsensitive(entry.key, L"{ushort=271}") ||
                TextContainsInsensitive(entry.key, L"{ushort=272}") ||
                TextContainsInsensitive(entry.key, L"{ushort=306}") ||
                TextContainsInsensitive(entry.key, L"{ushort=33434}") ||
                TextContainsInsensitive(entry.key, L"{ushort=33437}") ||
                TextContainsInsensitive(entry.key, L"{ushort=34855}") ||
                TextContainsInsensitive(entry.key, L"{ushort=37386}"))
            {
                return true;
            }
        }

        return false;
    }

    if (label == L"XMP")
        return TextContainsInsensitive(meta.containerNotes, L"XMP APP1 detectado");

    if (label == L"ICC")
    {
        return
            TextContainsInsensitive(meta.containerNotes, L"ICC profile APP2 detectado") ||
            TextContainsInsensitive(meta.containerNotes, L"ICC profile") ||
            TextContainsInsensitive(meta.containerNotes, L"ICC_PROFILE") ||
            TextContainsInsensitive(meta.containerNotes, L"ICC=1");
    }

    if (label == L"IPTC")
        return TextContainsInsensitive(meta.containerNotes, L"APP13/IPTC detectado");

    return false;
}

static bool HasXmpPhotoshopMetadata(const MetadataInfo& meta)
{
    for (const auto& entry : meta.entries)
    {
        if (TextContainsInsensitive(entry.key, L"photoshop:") ||
            TextContainsInsensitive(entry.section, L"photoshop:") ||
            TextContainsInsensitive(entry.value, L"xmlns:photoshop"))
        {
            return true;
        }
    }

    return TextContainsInsensitive(meta.containerNotes, L"xmlns:photoshop") ||
           TextContainsInsensitive(meta.containerNotes, L"photoshop:");
}

static std::wstring YesNo(bool value)
{
    return value ? L"sí" : L"no";
}


std::wstring MainWindow::BuildFingerprintText() const
{
    if (!state_.HasImage())
        return L"SilentPixel - huella\r\nSin imagen cargada.";

    const auto& img = state_.Image();
    const auto& meta = img.metadata;

    const std::wstring aHash = BuildAverageHash64(img);
    const std::wstring dHash = BuildDifferenceHash64(img);
    const std::wstring pixelHash = BuildDecodedPixelHash64(img);

    const bool hasExif = HasContainerMarkerExact(meta, L"EXIF");
    const bool hasXmp = HasContainerMarkerExact(meta, L"XMP");
    const bool hasIcc = HasContainerMarkerExact(meta, L"ICC");
    const bool hasIptc = HasContainerMarkerExact(meta, L"IPTC");
    const bool hasXmpPhotoshop = HasXmpPhotoshopMetadata(meta);

    std::wstringstream ss;
    ss << L"SilentPixel - huella\r\n\r\n";

    ss << L"Archivo: " << img.fileName << L"\r\n";
    ss << L"Formato: " << (meta.formatName.empty() ? L"No disponible" : meta.formatName) << L"\r\n";
    ss << L"Resolución: " << img.width << L"x" << img.height << L" px\r\n\r\n";

    ss << L"Exacto\r\n";
    ss << L"SHA-256: " << img.hashes.sha256 << L"\r\n";
    ss << L"SHA-1: " << img.hashes.sha1 << L"\r\n";
    ss << L"MD5: " << img.hashes.md5 << L"\r\n";
    ss << L"PixelHash64: " << pixelHash << L"\r\n\r\n";

    ss << L"Parecido visual\r\n";
    ss << L"aHash64: " << aHash << L"\r\n";
    ss << L"dHash64: " << dHash << L"\r\n\r\n";

    ss << L"Contenedor\r\n";
    ss << L"EXIF: " << YesNo(hasExif) << L" | "
       << L"XMP: " << YesNo(hasXmp) << L" | "
       << L"ICC: " << YesNo(hasIcc) << L" | "
       << L"IPTC: " << YesNo(hasIptc) << L" | "
       << L"XMP Photoshop: " << YesNo(hasXmpPhotoshop) << L"\r\n";

    ss << L"Ubicación: " << (meta.HasGps() ? L"sí" : L"no") << L"\r\n";
    ss << L"\r\n";

    ss << L"Uso: SHA-256, SHA-1 y MD5 identifican el archivo exacto. PixelHash64 identifica los p\u00EDxeles decodificados. aHash/dHash ayudan a comparar parecido visual aunque cambie la compresi\u00F3n o el tama\u00F1o.\r\n";

    return ss.str();
}



std::wstring MainWindow::BuildPanelReportText() const
{
    if (!state_.HasImage())
        return L"Sin imagen cargada.\r\n\r\nArrastra una imagen compatible aquí o pulsa Abrir.\r\n\r\nEste panel mostrará un resumen rápido. Usa Exportar TXT para el informe completo.";

    const auto& img = state_.Image();
    const auto& meta = img.metadata;
    const UINT reportW = img.originalWidth ? img.originalWidth : img.width;
    const UINT reportH = img.originalHeight ? img.originalHeight : img.height;

    auto cleanOrNA = [](const std::wstring& value) -> std::wstring
    {
        return value.empty() ? L"No disponible" : value;
    };

    auto section = [](std::wstringstream& ss, const std::wstring& title)
    {
        ss << title << L"\r\n";
    };

    std::wstringstream ss;
    section(ss, L"SilentPixel - Informe técnico del archivo");
    ss << L"\r\n";

    ss << L"Nombre: " << img.fileName << L"\r\n";
    if (img.preview)
        ss << L"Estado: modo ligero para navegar fluido. Datos completos al exportar/copiar.\r\n";
    ss << L"Ruta completa: oculta en el panel. Usa Copiar ruta solo si necesitas extraerla.\r\n";
    ss << L"Resolución: " << reportW << L"x" << reportH << L" px\r\n";
    ss << L"Tamaño archivo: " << FormatFileSize(img.fileSize) << L"\r\n";
    ss << L"Creado: " << FormatFileTimeLocal(img.created) << L"\r\n";
    ss << L"Modificado: " << FormatFileTimeLocal(img.modified) << L"\r\n\r\n";

    section(ss, L"Hashes");
    ss << L"SHA-256: " << img.hashes.sha256 << L"\r\n";
    ss << L"SHA-1: " << img.hashes.sha1 << L"\r\n";
    ss << L"MD5: " << img.hashes.md5 << L"\r\n";
    ss << L"Huella visual: disponible en Copiar huella o Exportar TXT.\r\n\r\n";

    section(ss, L"Contenedor rápido");
    const bool hasExif = HasContainerMarkerExact(meta, L"EXIF");
    const bool hasXmp = HasContainerMarkerExact(meta, L"XMP");
    const bool hasIcc = HasContainerMarkerExact(meta, L"ICC");
    const bool hasIptc = HasContainerMarkerExact(meta, L"IPTC");
    const bool hasXmpPhotoshop = HasXmpPhotoshopMetadata(meta);
    ss << L"EXIF: " << YesNo(hasExif) << L" | "
       << L"XMP: " << YesNo(hasXmp) << L" | "
       << L"ICC: " << YesNo(hasIcc) << L" | "
       << L"IPTC: " << YesNo(hasIptc) << L" | "
       << L"XMP Photoshop: " << YesNo(hasXmpPhotoshop) << L"\r\n";
    ss << L"Ubicación: " << YesNo(meta.HasGps()) << L"\r\n\r\n";

    section(ss, L"Resumen principal");
    ss << L"Dispositivo: " << (meta.make.empty() && meta.model.empty() ? L"No disponible" : (meta.make.empty() ? meta.model : (meta.model.empty() ? meta.make : meta.make + L" " + meta.model))) << L"\r\n";
    ss << L"Software: " << cleanOrNA(meta.software) << L"\r\n";
    ss << L"Fecha original: " << cleanOrNA(meta.dateOriginal) << L"\r\n";

    if (meta.HasShootingData())
    {
        ss << L"Disparo: ";
        bool first = true;
        auto add = [&](const std::wstring& label, const std::wstring& value)
        {
            if (value.empty())
                return;
            if (!first)
                ss << L" | ";
            ss << label << L": " << value;
            first = false;
        };

        add(L"ISO", meta.isoSpeed);
        add(L"Diafragma", meta.fNumber);
        add(L"Velocidad", meta.exposureTime);
        add(L"Focal", meta.focalLength);
        ss << L"\r\n";
    }

    ss << L"Coordenadas: " << (meta.HasGps() ? meta.GpsText() : L"No disponible") << L"\r\n";
    if (meta.HasGps())
        ss << L"Aviso: contiene ubicación. SilentPixel no abre mapas ni llama APIs externas.\r\n";
    ss << L"\r\n";

    section(ss, L"Datos técnicos");
    ss << L"Formato: " << cleanOrNA(meta.formatName) << L"\r\n";
    ss << L"Formato de píxel: " << cleanOrNA(meta.pixelFormat) << L"\r\n";
    ss << L"Profundidad estimada: " << (meta.bitsPerPixel == 0 ? L"No disponible" : std::to_wstring(meta.bitsPerPixel) + L" bpp") << L"\r\n";
    ss << L"Orientación EXIF: " << meta.ExifOrientationText(false) << L"\r\n";
    ss << L"Perfil de color: " << (meta.colorProfile.empty() ? L"No detectado" : meta.colorProfile) << L"\r\n\r\n";

    if (meta.HasShootingData())
    {
        section(ss, L"Parámetros de disparo");
        ss << L"ISO: " << cleanOrNA(meta.isoSpeed) << L"\r\n";
        ss << L"Diafragma: " << cleanOrNA(meta.fNumber) << L"\r\n";
        ss << L"Velocidad obturación: " << cleanOrNA(meta.exposureTime) << L"\r\n";
        ss << L"Longitud focal: " << cleanOrNA(meta.focalLength) << L"\r\n";
        ss << L"Equivalente 35 mm: " << (meta.focalLength35mm.empty() ? L"No disponible" : meta.focalLength35mm + L" mm") << L"\r\n";
        ss << L"Modo exposición: " << cleanOrNA(meta.exposureMode) << L"\r\n";
        ss << L"Medición: " << cleanOrNA(meta.meteringMode) << L"\r\n";
        ss << L"Balance blancos: " << cleanOrNA(meta.whiteBalance) << L"\r\n";
        ss << L"Flash: " << cleanOrNA(meta.flash) << L"\r\n";
        ss << L"Lente: " << cleanOrNA(meta.lensModel) << L"\r\n\r\n";
    }

    ss << L"Lectura avanzada: disponible en Exportar TXT.\r\n";

    return ss.str();
}


std::wstring MainWindow::BuildFullReportText() const
{
    if (!state_.HasImage())
        return L"Sin imagen cargada.\r\n\r\nArrastra una imagen compatible aqu\u00ED o pulsa Abrir.\r\n\r\nEste panel mostrar\u00E1 resumen principal, metadatos, hashes y an\u00E1lisis t\u00E9cnico del contenedor.";

    const auto& img = state_.Image();
    const UINT reportW = img.originalWidth ? img.originalWidth : img.width;
    const UINT reportH = img.originalHeight ? img.originalHeight : img.height;

    std::wstring metadata = img.metadata.FullText();

    auto section = [](std::wstringstream& ss, const std::wstring& title)
    {
        ss << title << L"\r\n";
    };

    std::wstringstream ss;
    section(ss, L"SilentPixel - Informe t\u00E9cnico del archivo");
    ss << L"\r\n";
    ss << L"Nombre: " << img.fileName << L"\r\n";
    ss << L"Ruta completa: oculta en el panel. Usa Copiar ruta solo si necesitas extraerla.\r\n";
    ss << L"Resoluci\u00F3n: " << reportW << L"x" << reportH << L" px\r\n";
    ss << L"Tama\u00F1o archivo: " << FormatFileSize(img.fileSize) << L"\r\n";
    ss << L"Creado: " << FormatFileTimeLocal(img.created) << L"\r\n";
    ss << L"Modificado: " << FormatFileTimeLocal(img.modified) << L"\r\n\r\n";

    section(ss, L"Hashes y huella");
    ss << L"SHA-256: " << img.hashes.sha256 << L"\r\n";
    ss << L"SHA-1: " << img.hashes.sha1 << L"\r\n";
    ss << L"MD5: " << img.hashes.md5 << L"\r\n";
    ss << L"PixelHash64 decodificado: " << BuildDecodedPixelHash64(img) << L"\r\n";
    ss << L"aHash64 visual: " << BuildAverageHash64(img) << L"\r\n";
    ss << L"dHash64 visual: " << BuildDifferenceHash64(img) << L"\r\n";
    ss << L"Uso: SHA-256, SHA-1 y MD5 identifican el archivo exacto. PixelHash64 identifica los p\u00EDxeles decodificados. aHash/dHash ayudan a comparar parecido visual aunque cambie la compresi\u00F3n o el tama\u00F1o.\r\n\r\n";

    section(ss, L"Contenedor r\u00E1pido");
    const bool hasExif = HasContainerMarkerExact(img.metadata, L"EXIF");
    const bool hasXmp = HasContainerMarkerExact(img.metadata, L"XMP");
    const bool hasIcc = HasContainerMarkerExact(img.metadata, L"ICC");
    const bool hasIptc = HasContainerMarkerExact(img.metadata, L"IPTC");
    const bool hasXmpPhotoshop = HasXmpPhotoshopMetadata(img.metadata);

    ss << L"EXIF: " << (hasExif ? L"s\u00ED" : L"no") << L" | "
       << L"XMP: " << (hasXmp ? L"s\u00ED" : L"no") << L" | "
       << L"ICC: " << (hasIcc ? L"s\u00ED" : L"no") << L" | "
       << L"IPTC: " << (hasIptc ? L"s\u00ED" : L"no") << L" | "
       << L"XMP Photoshop: " << (hasXmpPhotoshop ? L"s\u00ED" : L"no") << L"\r\n";
    ss << L"Ubicaci\u00F3n: " << (img.metadata.HasGps() ? L"s\u00ED" : L"no") << L"\r\n";
    ss << L"\r\n";
    ss << metadata;
    return ss.str();
}


std::wstring MainWindow::BuildExportReportText() const
{
    std::wstring source = BuildFullReportText();
    std::wstringstream in(source);
    std::wstringstream out;
    std::wstring line;

    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();

        if (line.rfind(L"Ruta completa:", 0) == 0)
            continue;

        if (line.rfind(L"Aviso: esta imagen contiene ubicación.", 0) == 0)
            continue;

        if (line.rfind(L"Aviso: esta imagen contiene coordenadas.", 0) == 0)
            continue;

        out << line << L"\r\n";
    }

    return out.str();
}

std::wstring MainWindow::AskSavePath(const wchar_t* filter, const wchar_t* defaultExt, const std::wstring& suggestedName)
{
    wchar_t file[MAX_PATH]{};
    wcsncpy_s(file, suggestedName.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_DONTADDTORECENT;

    if (GetSaveFileNameW(&ofn))
        return file;
    return L"";
}

std::wstring MainWindow::BuildCleanExportReportText() const
{
    std::wstringstream input(BuildFullReportText());
    std::wstringstream output;
    std::wstring line;

    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r')
            line.pop_back();

        if (ShouldDropReportLine(line))
            continue;

        output << line << L"\r\n";
    }

    return output.str();
}

void MainWindow::ExportReportTxt()
{
    if (!state_.HasImage())
        return;
    std::wstring suggested = FileStem(state_.Image().fileName) + L"_informe_silentpixel.txt";
    std::wstring path = AskSavePath(L"Texto UTF-8\0*.txt\0Todos los archivos\0*.*\0", L"txt", suggested);
    if (path.empty())
        return;
    Utf8WriteFile(path, BuildExportReportText());
}

void MainWindow::SaveCleanCopy()
{
    if (!state_.HasImage())
        return;
    std::wstring suggested = FileStem(state_.Image().fileName) + L"_limpia.png";
    std::wstring path = AskSavePath(L"PNG limpio\0*.png\0JPEG limpio\0*.jpg;*.jpeg\0BMP limpio\0*.bmp\0Todos los archivos\0*.*\0", L"png", suggested);
    if (path.empty())
        return;
    std::wstring error;
    if (!cleanExporter_->SaveCleanCopy(state_.Image(), path, error))
        MessageBoxW(hwnd_, error.c_str(), L"SilentPixel", MB_ICONERROR);
}

RECT MainWindow::ImageArea() const
{
    RECT client{};
    GetClientRect(hwnd_, &client);
    RECT rc{};
    rc.left = client.left;
    rc.top = TOP_BAR;
    rc.right = client.right;
    rc.bottom = client.bottom - STATUS_H;
    if (state_.MetadataVisible())
        rc.right -= META_WIDTH;
    if (rc.right < rc.left) rc.right = rc.left;
    if (rc.bottom < rc.top) rc.bottom = rc.top;
    return rc;
}

void MainWindow::PaintEmpty(HDC dc, const RECT& rc)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, MUTED);
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, uiFont_));
    std::wstring text = L"Arrastra una imagen compatible aqu\u00ED o pulsa Abrir";
    DrawTextW(dc, text.c_str(), -1, const_cast<RECT*>(&rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}

void MainWindow::PaintImage(HDC dc, const RECT& rc)
{
    const std::vector<BYTE>* pixelSource = &state_.DisplayPixels();
    int srcTotalW = state_.DisplayWidth();
    int srcTotalH = state_.DisplayHeight();
    UINT srcStride = state_.DisplayStride();

    if (state_.FitToWindow() &&
        state_.Rotation() == 0 &&
        state_.HasImage() &&
        !state_.Image().preview &&
        !paintCachePixels_.empty() &&
        paintCachePath_ == state_.Image().path)
    {
        pixelSource = &paintCachePixels_;
        srcTotalW = static_cast<int>(paintCacheWidth_);
        srcTotalH = static_cast<int>(paintCacheHeight_);
        srcStride = paintCacheStride_;
    }

    const auto& pixels = *pixelSource;

    if (pixels.empty() || srcTotalW == 0 || srcTotalH == 0 || srcStride == 0)
        return;

    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, rc.left, rc.top, rc.right, rc.bottom);

    double scale = state_.Zoom();
    if (state_.FitToWindow())
    {
        const double sx = static_cast<double>(rc.right - rc.left) / static_cast<double>(srcTotalW);
        const double sy = static_cast<double>(rc.bottom - rc.top) / static_cast<double>(srcTotalH);
        scale = std::min(sx, sy);
        if (scale <= 0.0)
            scale = 1.0;
    }

    int w = static_cast<int>(srcTotalW * scale);
    int h = static_cast<int>(srcTotalH * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    ClampPanToImageArea();

    const int x = rc.left + ((rc.right - rc.left) - w) / 2 + panX_;
    const int y = rc.top + ((rc.bottom - rc.top) - h) / 2 + panY_;

    RECT dest{};
    dest.left = x;
    dest.top = y;
    dest.right = x + w;
    dest.bottom = y + h;

    RECT visible{};
    visible.left = std::max(dest.left, rc.left);
    visible.top = std::max(dest.top, rc.top);
    visible.right = std::min(dest.right, rc.right);
    visible.bottom = std::min(dest.bottom, rc.bottom);

    if (visible.right <= visible.left || visible.bottom <= visible.top)
    {
        RestoreDC(dc, savedDc);
        return;
    }

    const int visibleW = visible.right - visible.left;
    const int visibleH = visible.bottom - visible.top;

    auto mapFloor = [](int distance, int srcTotal, int dstTotal) -> int
    {
        if (dstTotal <= 0)
            return 0;
        return static_cast<int>((static_cast<long long>(distance) * srcTotal) / dstTotal);
    };

    auto mapCeil = [](int distance, int srcTotal, int dstTotal) -> int
    {
        if (dstTotal <= 0)
            return srcTotal;
        return static_cast<int>((static_cast<long long>(distance) * srcTotal + dstTotal - 1) / dstTotal);
    };

    int srcX = mapFloor(visible.left - dest.left, srcTotalW, w);
    int srcY = mapFloor(visible.top - dest.top, srcTotalH, h);
    int srcRight = mapCeil(visible.right - dest.left, srcTotalW, w);
    int srcBottom = mapCeil(visible.bottom - dest.top, srcTotalH, h);

    srcX = std::clamp(srcX, 0, srcTotalW - 1);
    srcY = std::clamp(srcY, 0, srcTotalH - 1);
    srcRight = std::clamp(srcRight, srcX + 1, srcTotalW);
    srcBottom = std::clamp(srcBottom, srcY + 1, srcTotalH);

    const int srcW = srcRight - srcX;
    const int srcH = srcBottom - srcY;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(srcTotalW);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(srcTotalH);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    const unsigned long long pixelCount = static_cast<unsigned long long>(srcTotalW) * static_cast<unsigned long long>(srcTotalH);
    const bool usingPaintCache = pixelSource == &paintCachePixels_;
    const bool heavyDownscale = usingPaintCache || pixelCount >= 16000000ull || scale < 0.35;
    SetStretchBltMode(dc, (panning_ || heavyDownscale) ? COLORONCOLOR : HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(
        dc,
        visible.left,
        visible.top,
        visibleW,
        visibleH,
        srcX,
        srcY,
        srcW,
        srcH,
        pixels.data(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(48, 52, 60));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, GetStockObject(HOLLOW_BRUSH)));
    Rectangle(dc, x, y, x + w, y + h);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    RestoreDC(dc, savedDc);
}

std::wstring MainWindow::FormatFileSize(ULONGLONG bytes)
{
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB" };
    double size = static_cast<double>(bytes);
    int unit = 0;
    while (size >= 1024.0 && unit < 3)
    {
        size /= 1024.0;
        ++unit;
    }
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(unit == 0 ? 0 : 2) << size << L" " << units[unit];
    return ss.str();
}

std::wstring MainWindow::FormatFileTimeLocal(const FILETIME& ft)
{
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return L"No disponible";
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
    return buffer;
}

std::wstring MainWindow::DirectoryFromPath(const std::wstring& path)
{
    try
    {
        return std::filesystem::path(path).parent_path().wstring();
    }
    catch (...)
    {
        return L"";
    }
}

std::wstring MainWindow::FileStem(const std::wstring& path)
{
    try
    {
        return std::filesystem::path(path).stem().wstring();
    }
    catch (...)
    {
        return L"imagen";
    }
}


void MainWindow::SubclassInteractiveButton(HWND button)
{
    if (!button)
        return;
    SetWindowSubclass(button, ButtonSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
}

bool MainWindow::IsAppButton(HWND button) const
{
    HWND buttons[] = { btnOpen_, btnPrev_, btnNext_, btnZoomIn_, btnZoomOut_, btnFit_, btnActual_, btnRotate_, btnFull_, btnMeta_, btnCopySummary_, btnCopyAll_, btnCopyPath_, btnCopyHash_, btnExportTxt_, btnCleanCopy_, btnCopyGps_, btnOpenFolder_ };
    for (HWND item : buttons)
    {
        if (item == button)
            return true;
    }
    return false;
}

HWND MainWindow::MakeButton(HWND parent, int id, const wchar_t* text, HINSTANCE instance, HFONT font)
{
    HWND button = CreateWindowW(
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
        0, 0, 80, 28,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance,
        nullptr);
    if (font)
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return button;
}



















