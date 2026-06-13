#include "netfetch.h"

#ifdef _WIN32
// No raylib include in this TU, so windows.h is safe here. WIN32_LEAN_AND_MEAN
// trims the include set; NOMINMAX keeps the min/max macros out.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <urlmon.h>  // URLDownloadToFileA — linked via urlmon (see CMakeLists)
#else
#include <cstdlib>
#endif

namespace netfetch {

bool Download(const char* url, const std::string& dest) {
#ifdef _WIN32
    // URLDownloadToFile follows the HuggingFace HTTPS redirect.
    return URLDownloadToFileA(nullptr, url, dest.c_str(), 0, nullptr) == S_OK;
#else
    // curl ships with every desktop Linux and modern macOS.
    const std::string cmd = "curl --location --fail --silent --output \"" + dest + "\" \"" +
                            std::string(url) + "\"";
    return std::system(cmd.c_str()) == 0;
#endif
}

}  // namespace netfetch
