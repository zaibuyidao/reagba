#pragma once
#include <string_view>

namespace reagba {
// These values are also used by the embedded GPU shaders.
enum class ShaderPreset { None = 0, LCD3x = 1, LCDGridV2 = 2 };
inline bool IsShaderPreset(std::string_view name) {
    return name == "none" || name == "lcd3x" || name == "lcd-grid-v2";
}
inline ShaderPreset ParseShaderPreset(std::string_view name) {
    if (name == "lcd3x") return ShaderPreset::LCD3x;
    if (name == "lcd-grid-v2") return ShaderPreset::LCDGridV2;
    return ShaderPreset::None;
}
inline const char *ShaderPresetName(ShaderPreset preset) {
    switch (preset) {
    case ShaderPreset::LCD3x: return "lcd3x";
    case ShaderPreset::LCDGridV2: return "lcd-grid-v2";
    default: return "none";
    }
}
}
