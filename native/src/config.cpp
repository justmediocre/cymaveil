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
        theme = j.value("theme", theme);
        if (theme != "system" && theme != "light" && theme != "dark") theme = "system";
        canvasVisualizer = j.value("canvasVisualizer", canvasVisualizer);
        glassBlur = j.value("glassBlur", glassBlur);
        ambientGlow = j.value("ambientGlow", ambientGlow);
        bassShake = j.value("bassShake", bassShake);
        bassStrength = std::clamp(j.value("bassStrength", bassStrength), 10, 200);
        bassSensitivity = std::clamp(j.value("bassSensitivity", bassSensitivity), 0, 100);
        bassSpringiness = std::clamp(j.value("bassSpringiness", bassSpringiness), 0, 100);
        vinylDisc = j.value("vinylDisc", vinylDisc);
        mosaicEnabled = j.value("mosaicEnabled", mosaicEnabled);
        mosaicFlat = j.value("mosaicFlat", mosaicFlat);
        mosaicOpacity = std::clamp(j.value("mosaicOpacity", mosaicOpacity), 0.0f, 1.0f);
        mosaicDensity = std::clamp(j.value("mosaicDensity", mosaicDensity), 4, 14);
        mosaicTransition = j.value("mosaicTransition", mosaicTransition);
        visualizerStyle = j.value("visualizerStyle", visualizerStyle);
        visualizerIntensity = std::clamp(j.value("visualizerIntensity", visualizerIntensity), 10, 100);
        depthLayers = j.value("depthLayers", depthLayers);
        mpris = j.value("mpris", mpris);
        sidebarOpen = j.value("sidebarOpen", sidebarOpen);
        queuePanel = j.value("queuePanel", queuePanel);
    } catch (const std::exception&) {
        // Corrupt config: fall back to defaults.
    }
}

void Config::Save() const {
    nlohmann::json j{
        {"volume", volume},
        {"shuffle", shuffle},
        {"repeat", repeat},
        {"theme", theme},
        {"canvasVisualizer", canvasVisualizer},
        {"glassBlur", glassBlur},
        {"ambientGlow", ambientGlow},
        {"bassShake", bassShake},
        {"bassStrength", bassStrength},
        {"bassSensitivity", bassSensitivity},
        {"bassSpringiness", bassSpringiness},
        {"vinylDisc", vinylDisc},
        {"mosaicEnabled", mosaicEnabled},
        {"mosaicFlat", mosaicFlat},
        {"mosaicOpacity", mosaicOpacity},
        {"mosaicDensity", mosaicDensity},
        {"mosaicTransition", mosaicTransition},
        {"visualizerStyle", visualizerStyle},
        {"visualizerIntensity", visualizerIntensity},
        {"depthLayers", depthLayers},
        {"mpris", mpris},
        {"sidebarOpen", sidebarOpen},
        {"queuePanel", queuePanel},
    };
    std::ofstream out(paths::ConfigFile());
    out << j.dump(2) << '\n';
}
