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
        depthLayers = j.value("depthLayers", depthLayers);
        vinylDisc = j.value("vinylDisc", vinylDisc);
        queuePanel = j.value("queuePanel", queuePanel);
        mosaicEnabled = j.value("mosaicEnabled", mosaicEnabled);
        mosaicOpacity = std::clamp(j.value("mosaicOpacity", mosaicOpacity), 0.0f, 1.0f);
        mosaicDensity = std::clamp(j.value("mosaicDensity", mosaicDensity), 2, 16);
        mosaicTransition = j.value("mosaicTransition", mosaicTransition);
        mosaicFlat = j.value("mosaicFlat", mosaicFlat);
    } catch (const std::exception&) {
        // Corrupt config: fall back to defaults.
    }
}

void Config::Save() const {
    nlohmann::json j{
        {"volume", volume},
        {"shuffle", shuffle},
        {"repeat", repeat},
        {"depthLayers", depthLayers},
        {"vinylDisc", vinylDisc},
        {"queuePanel", queuePanel},
        {"mosaicEnabled", mosaicEnabled},
        {"mosaicOpacity", mosaicOpacity},
        {"mosaicDensity", mosaicDensity},
        {"mosaicTransition", mosaicTransition},
        {"mosaicFlat", mosaicFlat},
    };
    std::ofstream out(paths::ConfigFile());
    out << j.dump(2) << '\n';
}
