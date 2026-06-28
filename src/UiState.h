#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "ImageLoader.h"

class UiState
{
public:
    void SetImage(LoadedImage&& image);
    void Clear();

    bool HasImage() const;
    const LoadedImage& Image() const;
    LoadedImage& Image();

    const std::vector<BYTE>& DisplayPixels() const;
    UINT DisplayWidth() const;
    UINT DisplayHeight() const;
    UINT DisplayStride() const;
    int Rotation() const;

    void RotateClockwise();
    void SetZoom(double zoom);
    void ZoomBy(double factor);
    double Zoom() const;
    void SetFitToWindow(bool value);
    bool FitToWindow() const;
    void ToggleMetadata();
    void SetMetadataVisible(bool visible);
    bool MetadataVisible() const;

    void SetFolderList(std::vector<std::wstring> files, size_t index);
    bool HasNext() const;
    bool HasPrevious() const;
    std::wstring NextPath() const;
    std::wstring PreviousPath() const;
    std::wstring MoveNext();
    std::wstring MovePrevious();
    size_t NavigationCount() const;

private:
    LoadedImage image_;
    bool hasImage_ = false;
    int rotation_ = 0;
    double zoom_ = 1.0;
    bool fitToWindow_ = true;
    bool metadataVisible_ = false;
    std::vector<BYTE> displayPixels_;
    UINT displayWidth_ = 0;
    UINT displayHeight_ = 0;
    UINT displayStride_ = 0;
    std::vector<std::wstring> folderFiles_;
    size_t folderIndex_ = 0;

    void RebuildDisplayPixels();
};
