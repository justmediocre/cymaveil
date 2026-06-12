cmake_minimum_required(VERSION 3.24...3.30)
project(cymaveil CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ── raylib ──
# NOTE: do not use CUSTOMIZE_BUILD to enable FLAC — it switches every optional
# feature to the CMake-option defaults, which enable SUPPORT_CUSTOM_FRAME_CONTROL
# (breaks EndDrawing frame pacing entirely) and SUPPORT_BUSY_WAIT_LOOP (spin
# waits). FLAC is enabled via a compile definition below instead.
set(RAYLIB_VERSION 6.0)

# Build GLFW with both backends; it picks Wayland at runtime when
# WAYLAND_DISPLAY is set and falls back to X11 otherwise.
set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)

# PATCH_COMMANDs: fix a double free in raylib 6.0's FLAC unload path, set the
# Wayland app_id / X11 WM_CLASS so desktops can match the window to
# cymaveil.desktop (icon + taskbar media controls), and fix HiDPI mouse scaling
# being wrongly re-applied on resize under Wayland; see cmake/patch-raylib-*.
FetchContent_Declare(
    raylib
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch-raylib-flac.cmake
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch-raylib-appid.cmake
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch-raylib-mousescale.cmake
)

# ── TagLib (metadata + embedded artwork) ──
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB OFF CACHE BOOL "" FORCE)

# Header-only dependency of TagLib
FetchContent_Declare(
    utfcpp
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/nemtrif/utfcpp/archive/refs/tags/v4.0.6.tar.gz
)
FetchContent_MakeAvailable(utfcpp)
set(utf8cpp_INCLUDE_DIR ${utfcpp_SOURCE_DIR}/source CACHE PATH "" FORCE)

FetchContent_Declare(
    taglib
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://taglib.org/releases/taglib-2.0.2.tar.gz
)

# ── ONNX Runtime (Depth Anything v2 inference for depth layers) ──
set(ORT_VERSION 1.20.1)
FetchContent_Declare(
    onnxruntime
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz
)

# ── nlohmann/json (library cache + settings persistence) ──
FetchContent_Declare(
    json
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_MakeAvailable(raylib taglib json onnxruntime)

# FLAC (music) and JPG (album art) are off in raylib's default config.h
target_compile_definitions(raylib PRIVATE
    SUPPORT_FILEFORMAT_FLAC=1
    SUPPORT_FILEFORMAT_JPG=1)

find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME})
add_subdirectory(src)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PROJECT_NAME})

add_library(ort SHARED IMPORTED)
set_target_properties(ort PROPERTIES
    IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so.${ORT_VERSION}
    INTERFACE_INCLUDE_DIRECTORIES ${onnxruntime_SOURCE_DIR}/include)

target_link_libraries(${PROJECT_NAME} PRIVATE
    raylib
    tag
    nlohmann_json::nlohmann_json
    ort
    Threads::Threads)

# ── libdbus (MPRIS / media keys) ──
# Optional: without it the Mpris class compiles to a stub. The runtime dep
# (libdbus-1.so.3) ships with every desktop Linux.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(DBUS IMPORTED_TARGET dbus-1)
endif()
if(DBUS_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CYMAVEIL_HAVE_DBUS=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::DBUS)
else()
    message(STATUS "dbus-1 not found - building without MPRIS support")
endif()

# Ship libonnxruntime next to the binary and resolve it via $ORIGIN so the
# build runs both inside the dev container and on the host.
set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "$ORIGIN")
file(GLOB ORT_SHARED_LIBS ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so*)
foreach(ORT_LIB ${ORT_SHARED_LIBS})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_LIB} $<TARGET_FILE_DIR:${PROJECT_NAME}>)
endforeach()

# Embed the app icon (shared with the electron build) so SetWindowIcon works
# without shipping a resource file next to the binary. Written to a staging
# file first so an unchanged icon doesn't dirty the header every configure.
set(ICON_PNG ${CMAKE_CURRENT_SOURCE_DIR}/../build/icon.png)
set(ICON_HEADER ${CMAKE_BINARY_DIR}/generated/icon_png.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ICON_PNG})
file(READ ${ICON_PNG} ICON_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," ICON_BYTES ${ICON_HEX})
file(WRITE ${ICON_HEADER}.in
    "// Generated from build/icon.png by CMake — do not edit.\n"
    "#pragma once\n\n"
    "inline constexpr unsigned char kIconPng[] = {${ICON_BYTES}};\n"
    "inline constexpr int kIconPngSize = sizeof(kIconPng);\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ICON_HEADER}.in ${ICON_HEADER})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)

# TagLib's build tree doesn't export usable include dirs, so wire them up here.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${taglib_SOURCE_DIR}/taglib
    ${taglib_SOURCE_DIR}/taglib/toolkit
    ${taglib_BINARY_DIR}
    ${taglib_BINARY_DIR}/taglib)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
