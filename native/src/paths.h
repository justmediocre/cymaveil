#pragma once

#include <string>

namespace paths {

// ~/.local/share/cymaveil (or $XDG_DATA_HOME/cymaveil); created on demand.
std::string DataDir();
// DataDir()/art — extracted album artwork cache.
std::string ArtDir();
std::string LibraryFile();
std::string ConfigFile();

}  // namespace paths
