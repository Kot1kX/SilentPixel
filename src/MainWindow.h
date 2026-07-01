#pragma once
#include <windows.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include "UiState.h"
#include "ImageLoader.h"
#include "CleanExportService.h"

class MainWindow
{
public:
    MainWindow();
    ~MainWindow();

    bool Create(HINSTANCE instance, int showCmd, const std::wstring& startupPath = L"");
    HWND Window() const;

private:
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    IWICImagingFactory* wicFactory_ = nullptr;
    ImageLoader* loader_ = nullptr;
    CleanExportService* cleanExporter_ = nullptr;
    UiState state_;

    HWND btnOpen_ = nullptr;
    HWND btnPrev_ = nullptr;
    HWND btnNext_ = nullptr;
    HWND btnZoomIn_ = nullptr;
    HWND btnZoomOut_ = nullptr;
    HWND btnFit_ = nullptr;
    HWND btnActual_ = nullptr;
    HWND btnRotate_ = nullptr;
    HWND btnFull_ = nullptr;
    HWND btnMeta_ = nullptr;
    HWND metaEdit_ = nullptr;
    HWND statusBar_ = nullptr;
    HWND btnCopySummary_ = nullptr;
    HWND btnCopyAll_ = nullptr;
    HWND btnCopyPath_ = nullptr;
    HWND btnCopyHash_ = nullptr;
    HWND btnExportTxt_ = nullptr;
    HWND btnCleanCopy_ = nullptr;
    HWND btnCopyGps_ = nullptr;
    HWND btnOpenFolder_ = nullptr;
    HWND hoverButton_ = nullptr;
    std::wstring pendingStartupPath_;
    std::wstring pendingFullLoadPath_;
    std::wstring statusText_;
    int panX_ = 0;
    int panY_ = 0;
    bool panning_ = false;
    bool navigationFromSelection_ = false;
    ULONGLONG navigationCooldownUntil_ = 0;
    POINT dragStart_{};
    POINT dragPanStart_{};

    std::vector<BYTE> paintCachePixels_;
    UINT paintCacheWidth_ = 0;
    UINT paintCacheHeight_ = 0;
    UINT paintCacheStride_ = 0;
    std::wstring paintCachePath_;

    HBRUSH bgBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    HFONT uiFont_ = nullptr;
    HFONT monoFont_ = nullptr;
    HFONT brandFont_ = nullptr;
    HFONT creditFont_ = nullptr;
    HFONT statusFont_ = nullptr;
    HFONT metaFont_ = nullptr;

    bool fullscreen_ = false;
    RECT windowedRect_{};
    LONG_PTR windowedStyle_ = 0;
    LONG_PTR windowedExStyle_ = 0;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnDestroy();
    void OnSize();
    void OnPaint();
    void OnCommand(WORD id);
    void OnDropFiles(HDROP drop);
    void OnMouseWheel(short delta, int screenX, int screenY);
    void OnLeftButtonDown(int x, int y);
    void OnMouseMove(int x, int y, WPARAM keys);
    void OnLeftButtonUp();
    void OnKeyDown(WPARAM key);
    HBRUSH OnCtlColor(HDC dc, HWND control);
    bool OnDrawItem(DRAWITEMSTRUCT* item);

    void OpenStartupPath();
    void OpenFileDialog();
    bool LoadImage(const std::wstring& path, bool rebuildFolder = true, bool quietErrors = false);
    bool LoadImageFull(const std::wstring& path, bool rebuildFolder = false, bool quietErrors = true);
    bool EnsureFullImageLoaded();
    void LoadNext();
    void LoadPrevious();
    bool CanNavigateNow() const;
    void MarkNavigationHandled();
    void RebuildFolderList(const std::wstring& path);
    void UpdateTitle();
    void ApplyMetadataVisibility();
    void ApplyMetadataTextPadding();
    void UpdateMetadataPanel();
    void UpdateStatus();
    void SetStatusText(const std::wstring& text);
    void PaintStatusLine(HDC dc) const;
    RECT StatusArea() const;
    void ToggleFullscreen();
    void LayoutControls();
    void InvalidateImageAreaOnly();
    void RedrawWholeWindow();
    void RedrawContentArea();
    void ResetPan();
    void ClampPanToImageArea();
    double CurrentFitZoom() const;
    void ZoomByFactor(double factor);
    void ClearPaintCache();
    void BuildPaintCacheIfNeeded();
    bool CanPanImage() const;
    void SubclassInteractiveButton(HWND button);
    bool IsAppButton(HWND button) const;

    std::wstring BuildSummaryText() const;
    std::wstring BuildFingerprintText() const;
    std::wstring BuildPanelReportText() const;
    std::wstring BuildFullReportText() const;
    std::wstring BuildCleanExportReportText() const;
    std::wstring BuildExportReportText() const;
    std::wstring AskSavePath(const wchar_t* filter, const wchar_t* defaultExt, const std::wstring& suggestedName);
    void ExportReportTxt();
    void SaveCleanCopy();

    RECT ImageArea() const;
    void PaintEmpty(HDC dc, const RECT& rc);
    void PaintImage(HDC dc, const RECT& rc);
    void DrawBrandSignature(HDC dc) const;

    static std::wstring FormatFileSize(ULONGLONG bytes);
    static std::wstring FormatFileTimeLocal(const FILETIME& ft);
    static std::wstring DirectoryFromPath(const std::wstring& path);
    static std::wstring FileStem(const std::wstring& path);
    static HWND MakeButton(HWND parent, int id, const wchar_t* text, HINSTANCE instance, HFONT font);
};



