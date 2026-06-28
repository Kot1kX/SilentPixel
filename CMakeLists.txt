cmake_minimum_required(VERSION 3.22)
project(SilentPixel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(SilentPixel WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/ImageLoader.cpp
    src/MetadataService.cpp
    src/HashService.cpp
    src/CleanExportService.cpp
    src/PrivacyGuard.cpp
    src/UiState.cpp
)

target_compile_definitions(SilentPixel PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    STRICT
)

target_include_directories(SilentPixel PRIVATE src)

target_link_libraries(SilentPixel PRIVATE
    windowscodecs
    ole32
    oleaut32
    shell32
    shlwapi
    comdlg32
    comctl32
    bcrypt
    user32
    gdi32
    advapi32
)

if(MSVC)
    target_compile_options(SilentPixel PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_link_options(SilentPixel PRIVATE /SUBSYSTEM:WINDOWS)
endif()
