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

# PATCH_COMMAND: fixes a double free in raylib 6.0's FLAC unload path
# (see cmake/patch-raylib-flac.cmake).
FetchContent_Declare(
    raylib
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch-raylib-flac.cmake
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

# Ship libonnxruntime next to the binary and resolve it via $ORIGIN so the
# build runs both inside the dev container and on the host.
set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "$ORIGIN")
file(GLOB ORT_SHARED_LIBS ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so*)
foreach(ORT_LIB ${ORT_SHARED_LIBS})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_LIB} $<TARGET_FILE_DIR:${PROJECT_NAME}>)
endforeach()

# TagLib's build tree doesn't export usable include dirs, so wire them up here.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${taglib_SOURCE_DIR}/taglib
    ${taglib_SOURCE_DIR}/taglib/toolkit
    ${taglib_BINARY_DIR}
    ${taglib_BINARY_DIR}/taglib)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()
