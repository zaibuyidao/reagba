#pragma once
#include "core/IEmulatorCore.h"
#include "video/ShaderPreset.h"
namespace reagba {
struct VideoSettings {
    bool integerScaling = true, linear = false, vsync = true;
    ShaderPreset shader = ShaderPreset::None;
};
struct GameViewport {
    double x = 0, y = 0, width = 0, height = 0, clipTop = 0, clipBottom = 0, clientWidth = 1;
    bool visible = false;
};
}
