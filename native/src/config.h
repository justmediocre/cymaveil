#pragma once

#include <string>
#include <vector>

// User settings persisted across sessions (volume, shuffle/repeat, etc.).
// Library folders live in the library cache; this is everything else.
struct Config {
    float volume = 0.8f;
    bool shuffle = false;
    int repeat = 0;  // 0=off 1=all 2=one

    void Load();
    void Save() const;
};
