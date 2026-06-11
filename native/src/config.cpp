#include "config.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "paths.h"

void Config::Load() {
    std::ifstream in(paths::ConfigFile());
    if (!in) return;
    try {
        nlohmann::json j = nlohmann::json::parse(in);
        volume = std::clamp(j.value("volume", volume), 0.0f, 1.0f);
        shuffle = j.value("shuffle", shuffle);
        repeat = std::clamp(j.value("repeat", repeat), 0, 2);
    } catch (const std::exception&) {
        // Corrupt config: fall back to defaults.
    }
}

void Config::Save() const {
    nlohmann::json j{
        {"volume", volume},
        {"shuffle", shuffle},
        {"repeat", repeat},
    };
    std::ofstream out(paths::ConfigFile());
    out << j.dump(2) << '\n';
}
