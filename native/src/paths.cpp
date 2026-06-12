#include "paths.h"

#include <cstdlib>
#include <filesystem>

namespace paths {

static std::string EnsureDir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}

std::string DataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = std::string(home ? home : ".") + "/.local/share";
    }
    return EnsureDir(base + "/cymaveil");
}

std::string ArtDir() { return EnsureDir(DataDir() + "/art"); }
std::string LibraryFile() { return DataDir() + "/library.json"; }
std::string ConfigFile() { return DataDir() + "/config.json"; }
std::string PlaylistsFile() { return DataDir() + "/playlists.json"; }

std::string MusicDir() {
    const char* home = std::getenv("HOME");
    const std::string base = home ? home : ".";
    const std::string music = base + "/Music";
    std::error_code ec;
    return std::filesystem::is_directory(music, ec) ? music : base;
}

}  // namespace paths
