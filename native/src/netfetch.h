#pragma once

#include <string>

namespace netfetch {

// Download url to dest over HTTPS (follows redirects). Returns true on success.
// Implemented in its own translation unit so the Windows backend can include
// <windows.h>/<urlmon.h> without colliding with raylib's API (Rectangle,
// LoadImage, DrawText, CloseWindow, ...).
bool Download(const char* url, const std::string& dest);

}  // namespace netfetch
