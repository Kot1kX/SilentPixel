#include "UiState.h"
#include "PrivacyGuard.h"
#include <algorithm>
#include <cstring>

void UiState::SetImage(LoadedImage&& image)
{
    Clear();
    image_ = std::move(image);
    hasImage_ = true;
    rotation_ = 0;
    zoom_ = 1.0;
    fitToWindow_ = true;
    RebuildDisplayPixels();
}

void UiState::Clear()
{
    PrivacyGuard::ZeroVector(image_.pixelsBGRA);
    PrivacyGuard::ZeroVector(displayPixels_);
    image_ = LoadedImage{};
    hasImage_ = false;
    rotation_ = 0;
    zoom_ = 1.0;
    fitToWindow_ = true;
}

bool UiState::HasImage() const { return hasImage_; }
const LoadedImage& UiState::Image() const { return image_; }
LoadedImage& UiState::Image() { return image_; }
const std::vector<BYTE>& UiState::DisplayPixels() const
{
    if (hasImage_ && rotation_ == 0 && displayPixels_.empty())
        return image_.pixelsBGRA;

    return displayPixels_;
}

UINT UiState::DisplayWidth() const { return displayWidth_; }
UINT UiState::DisplayHeight() const { return displayHeight_; }
UINT UiState::DisplayStride() const { return displayStride_; }
int UiState::Rotation() const { return rotation_; }

double UiState::Zoom() const { return zoom_; }
void UiState::SetFitToWindow(bool value) { fitToWindow_ = value; }
bool UiState::FitToWindow() const { return fitToWindow_; }
void UiState::ToggleMetadata() { metadataVisible_ = !metadataVisible_; }
void UiState::SetMetadataVisible(bool visible) { metadataVisible_ = visible; }
bool UiState::MetadataVisible() const { return metadataVisible_; }

void UiState::SetZoom(double zoom)
{
    zoom_ = std::clamp(zoom, 0.005, 16.0);
    fitToWindow_ = false;
}

void UiState::ZoomBy(double factor)
{
    SetZoom(zoom_ * factor);
}

void UiState::RotateClockwise()
{
    if (!hasImage_)
        return;
    rotation_ = (rotation_ + 90) % 360;
    RebuildDisplayPixels();
}

void UiState::SetFolderList(std::vector<std::wstring> files, size_t index)
{
    folderFiles_ = std::move(files);
    folderIndex_ = index < folderFiles_.size() ? index : 0;
}

bool UiState::HasNext() const
{
    return !folderFiles_.empty() && folderIndex_ + 1 < folderFiles_.size();
}

bool UiState::HasPrevious() const
{
    return !folderFiles_.empty() && folderIndex_ > 0;
}

std::wstring UiState::NextPath() const
{
    return HasNext() ? folderFiles_[folderIndex_ + 1] : L"";
}

size_t UiState::NavigationCount() const
{
    return folderFiles_.size();
}

std::wstring UiState::PreviousPath() const
{
    return HasPrevious() ? folderFiles_[folderIndex_ - 1] : L"";
}

std::wstring UiState::MoveNext()
{
    if (!HasNext())
        return L"";
    ++folderIndex_;
    return folderFiles_[folderIndex_];
}

std::wstring UiState::MovePrevious()
{
    if (!HasPrevious())
        return L"";
    --folderIndex_;
    return folderFiles_[folderIndex_];
}

void UiState::RebuildDisplayPixels()
{
    PrivacyGuard::ZeroVector(displayPixels_);
    displayWidth_ = 0;
    displayHeight_ = 0;
    displayStride_ = 0;

    if (!hasImage_ || image_.pixelsBGRA.empty())
        return;

    if (rotation_ == 0)
    {
        displayWidth_ = image_.width;
        displayHeight_ = image_.height;
        displayStride_ = image_.stride;
        return;
    }

    if (rotation_ == 180)
    {
        displayWidth_ = image_.width;
        displayHeight_ = image_.height;
        displayStride_ = displayWidth_ * 4;
        displayPixels_.resize(static_cast<size_t>(displayStride_) * displayHeight_);
        for (UINT y = 0; y < image_.height; ++y)
        {
            for (UINT x = 0; x < image_.width; ++x)
            {
                const size_t src = static_cast<size_t>(y) * image_.stride + static_cast<size_t>(x) * 4;
                const size_t dst = static_cast<size_t>(image_.height - 1 - y) * displayStride_ + static_cast<size_t>(image_.width - 1 - x) * 4;
                memcpy(displayPixels_.data() + dst, image_.pixelsBGRA.data() + src, 4);
            }
        }
        return;
    }

    displayWidth_ = image_.height;
    displayHeight_ = image_.width;
    displayStride_ = displayWidth_ * 4;
    displayPixels_.resize(static_cast<size_t>(displayStride_) * displayHeight_);

    for (UINT y = 0; y < image_.height; ++y)
    {
        for (UINT x = 0; x < image_.width; ++x)
        {
            const size_t src = static_cast<size_t>(y) * image_.stride + static_cast<size_t>(x) * 4;
            UINT dx = 0;
            UINT dy = 0;
            if (rotation_ == 90)
            {
                dx = image_.height - 1 - y;
                dy = x;
            }
            else
            {
                dx = y;
                dy = image_.width - 1 - x;
            }
            const size_t dst = static_cast<size_t>(dy) * displayStride_ + static_cast<size_t>(dx) * 4;
            memcpy(displayPixels_.data() + dst, image_.pixelsBGRA.data() + src, 4);
        }
    }
}
