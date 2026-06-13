cmake_minimum_required(VERSION 3.24...3.30)
project(cymaveil VERSION 0.1.0 LANGUAGES CXX)

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
# WAYLAND_DISPLAY is set and falls back to X11 otherwise. Linux-only — GLFW
# uses the native Win32 backend on Windows and ignores these.
if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
endif()

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
# Microsoft ships prebuilt binaries per platform; pick the matching archive.
# Windows is MSVC-built (.dll + .lib import library), Linux a versioned .so,
# macOS a universal2 .dylib (one build for Apple Silicon + Intel).
set(ORT_VERSION 1.20.1)
if(WIN32)
    set(ORT_URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip)
elseif(APPLE)
    set(ORT_URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-universal2-${ORT_VERSION}.tgz)
else()
    set(ORT_URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz)
endif()
FetchContent_Declare(
    onnxruntime
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL ${ORT_URL}
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
if(WIN32)
    set_target_properties(ort PROPERTIES
        IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll
        IMPORTED_IMPLIB ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib
        INTERFACE_INCLUDE_DIRECTORIES ${onnxruntime_SOURCE_DIR}/include)
elseif(APPLE)
    set_target_properties(ort PROPERTIES
        IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.${ORT_VERSION}.dylib
        INTERFACE_INCLUDE_DIRECTORIES ${onnxruntime_SOURCE_DIR}/include)
else()
    set_target_properties(ort PROPERTIES
        IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so.${ORT_VERSION}
        INTERFACE_INCLUDE_DIRECTORIES ${onnxruntime_SOURCE_DIR}/include)
endif()

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

# Ship the ONNX Runtime shared lib next to the binary so it resolves at runtime.
# Linux needs an $ORIGIN rpath and macOS an @loader_path rpath (both mean "next
# to me"); on Windows the executable's own directory is already on the DLL
# search path. With MACOSX_BUNDLE the binary lives in cymaveil.app/Contents/MacOS,
# and the POST_BUILD copy below puts the dylib right beside it.
if(WIN32)
    file(GLOB ORT_SHARED_LIBS ${onnxruntime_SOURCE_DIR}/lib/onnxruntime*.dll)
    # URLDownloadToFile (depth model fetch) lives in urlmon.
    target_link_libraries(${PROJECT_NAME} PRIVATE urlmon)
elseif(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    file(GLOB ORT_SHARED_LIBS ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime*.dylib)
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "$ORIGIN")
    file(GLOB ORT_SHARED_LIBS ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so*)
endif()
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

    # ── macOS .app bundle ──
    # SetWindowIcon is a no-op on macOS, so the Dock/Finder icon must come from
    # the bundle. Build cymaveil.icns from build/icon.png (512px source, so cap
    # the iconset at 512) using the system sips/iconutil, then drop it into the
    # bundle's Resources. The POST_BUILD ONNX-dylib copy above already targets
    # Contents/MacOS (TARGET_FILE_DIR resolves there once MACOSX_BUNDLE is on).
    set(ICNS_OUT ${CMAKE_BINARY_DIR}/cymaveil.icns)
    set(ICONSET_DIR ${CMAKE_BINARY_DIR}/cymaveil.iconset)
    add_custom_command(
        OUTPUT ${ICNS_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ICONSET_DIR}
        COMMAND sips -z 16 16   ${ICON_PNG} --out ${ICONSET_DIR}/icon_16x16.png
        COMMAND sips -z 32 32   ${ICON_PNG} --out ${ICONSET_DIR}/icon_16x16@2x.png
        COMMAND sips -z 32 32   ${ICON_PNG} --out ${ICONSET_DIR}/icon_32x32.png
        COMMAND sips -z 64 64   ${ICON_PNG} --out ${ICONSET_DIR}/icon_32x32@2x.png
        COMMAND sips -z 128 128 ${ICON_PNG} --out ${ICONSET_DIR}/icon_128x128.png
        COMMAND sips -z 256 256 ${ICON_PNG} --out ${ICONSET_DIR}/icon_128x128@2x.png
        COMMAND sips -z 256 256 ${ICON_PNG} --out ${ICONSET_DIR}/icon_256x256.png
        COMMAND sips -z 512 512 ${ICON_PNG} --out ${ICONSET_DIR}/icon_256x256@2x.png
        COMMAND sips -z 512 512 ${ICON_PNG} --out ${ICONSET_DIR}/icon_512x512.png
        COMMAND iconutil -c icns ${ICONSET_DIR} -o ${ICNS_OUT}
        DEPENDS ${ICON_PNG}
        COMMENT "Generating cymaveil.icns from icon.png")
    set_source_files_properties(${ICNS_OUT} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(${PROJECT_NAME} PRIVATE ${ICNS_OUT})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "Cymaveil"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.cymaveil.app"
        MACOSX_BUNDLE_ICON_FILE "cymaveil.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

    # ── .dmg packaging (unsigned) ──
    # `cpack` from the build dir produces Cymaveil-<ver>.dmg containing the .app.
    # install re-copies the target fresh, so the ONNX dylib must be installed into
    # the bundle explicitly (the POST_BUILD copy only touches the build tree).
    install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION .)
    install(FILES ${ORT_SHARED_LIBS} DESTINATION cymaveil.app/Contents/MacOS)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_PACKAGE_NAME "Cymaveil")
    set(CPACK_PACKAGE_FILE_NAME "Cymaveil-${PROJECT_VERSION}")
    set(CPACK_DMG_VOLUME_NAME "Cymaveil")
    include(CPack)
endif()
