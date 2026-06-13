#include "paths.h"

#include <cstdlib>
#include <filesystem>

namespace paths {

static std::string EnsureDir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}

std::string Home() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return home && *home ? home : ".";
}

std::string DataDir() {
#ifdef _WIN32
    // %APPDATA% (roaming) is the conventional per-user app data location.
    const char* appdata = std::getenv("APPDATA");
    const std::string base = appdata && *appdata ? appdata : Home();
    return EnsureDir(base + "/cymaveil");
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        base = Home() + "/.local/share";
    }
    return EnsureDir(base + "/cymaveil");
#endif
}

std::string ArtDir() { return EnsureDir(DataDir() + "/art"); }
std::string LibraryFile() { return DataDir() + "/library.json"; }
std::string ConfigFile() { return DataDir() + "/config.json"; }
std::string PlaylistsFile() { return DataDir() + "/playlists.json"; }
std::string SessionFile() { return DataDir() + "/session.json"; }

std::string MusicDir() {
    const std::string base = Home();
    const std::string music = base + "/Music";
    std::error_code ec;
    return std::filesystem::is_directory(music, ec) ? music : base;
}

}  // namespace paths
