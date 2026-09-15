#pragma once
#include "core/IEmulatorCore.h"
namespace reagba {
struct VideoSettings {
    bool integerScaling = true, linear = false, vsync = true;
};
struct GameViewport {
    double x = 0, y = 0, width = 0, height = 0, clipTop = 0, clipBottom = 0, clientWidth = 1;
    bool visible = false;
};
}
