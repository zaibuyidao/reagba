// Reproduce the X11 macro exposed by GTK's gtkx.h on every test platform.
#define None 0L
#include "video/VideoTypes.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

#ifndef None
#error Video headers must preserve X11's None macro
#endif
static_assert(None == 0L, "X11's None value must be preserved");

using namespace reagba;

int main() try {
    // Preset IDs are part of the interface to the embedded GPU shaders.
    const std::array<std::string_view, 3> names{"none", "lcd3x", "lcd-grid-v2"};
    for (size_t i = 0; i < names.size(); ++i) {
        const auto preset = ParseShaderPreset(names[i]);
        if (!IsShaderPreset(names[i]) || static_cast<int>(preset) != static_cast<int>(i) ||
            ShaderPresetName(preset) != names[i])
            throw std::runtime_error("Shader preset mapping changed");
    }
    const VideoSettings defaults;
    if (static_cast<int>(defaults.shader) != 0 ||
        defaults.shader != ParseShaderPreset("none"))
        throw std::runtime_error("Shaders must default to off");
    for (const auto name : {"", "broken", "LCD3x"}) {
        if (IsShaderPreset(name) || ParseShaderPreset(name) != defaults.shader)
            throw std::runtime_error("Invalid shader fallback changed");
    }
    if (std::string_view(ShaderPresetName(static_cast<ShaderPreset>(-1))) != "none")
        throw std::runtime_error("Unknown shader ID must map to off");
    std::cout << "PASS: X11 macro compatibility, shader IDs, names and defaults\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
