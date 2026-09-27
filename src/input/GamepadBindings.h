#pragma once
#include "rom/ROMManager.h"
#include <array>
#include <cstdint>

namespace reagba {
// Source bits are independent of SDL button indices and shared with the settings UI.
inline constexpr std::array<const char*, 26> GamepadSources = {
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
    "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "touchpad",
    "leftup", "leftdown", "leftleft", "leftright", "rightup", "rightdown", "rightleft", "rightright",
    "lefttrigger", "righttrigger"};
inline constexpr std::array<const char*, 10> GamepadTargets = {
    "a", "b", "select", "start", "right", "left", "up", "down", "r", "l"};
inline Json DefaultGamepadBindings() {
    static constexpr int targets[] = {0, 1, -1, -1, 2, -1, 3, -1, -1, 9, 8, 6, 7, 5, 4, -1,
                                       6, 7, 5, 4, -1, -1, -1, -1, -1, -1};
    Json result = Json::object();
    for (size_t i = 0; i < GamepadSources.size(); ++i)
        result[GamepadSources[i]] = {{"target", targets[i] < 0 ? "none" : GamepadTargets[targets[i]]}, {"mode", "hold"}};
    return result;
}
inline Json NormalizeGamepadBindings(const Json& value) {
    if (!value.is_object()) throw std::runtime_error("Gamepad bindings must be an object");
    auto result = DefaultGamepadBindings();
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!result.contains(it.key())) throw std::runtime_error("Unknown gamepad input");
        const auto& binding = it.value();
        if (!binding.is_object() || binding.size() != 2 || !binding.contains("target") || !binding.contains("mode"))
            throw std::runtime_error("Invalid gamepad binding");
        bool valid = binding["target"] == "none";
        for (auto target : GamepadTargets) valid |= binding["target"] == target;
        if (!valid) throw std::runtime_error("Unknown gamepad target");
        if (binding["mode"] != "hold" && binding["mode"] != "turbo" && binding["mode"] != "single")
            throw std::runtime_error("Unknown gamepad trigger mode");
        result[it.key()] = binding;
    }
    return result;
}

class GamepadBindings {
    struct Binding { uint32_t mask = 0; int mode = 0; uint64_t started = 0; };
    std::array<Binding, GamepadSources.size()> bindings_{};
    uint32_t held_ = 0;
  public:
    GamepadBindings() { Configure(DefaultGamepadBindings()); }
    void Configure(const Json& config) {
        const auto normalized = NormalizeGamepadBindings(config);
        for (size_t i = 0; i < bindings_.size(); ++i) {
            const auto& value = normalized[GamepadSources[i]];
            auto& binding = bindings_[i];
            binding.mask = 0;
            for (size_t j = 0; j < GamepadTargets.size(); ++j)
                if (value["target"] == GamepadTargets[j]) binding.mask = 1u << j;
            binding.mode = value["mode"] == "turbo" ? 1 : value["mode"] == "single" ? 2 : 0;
        }
    }
    // Evaluate on the emulation thread so a single press lasts one game frame.
    uint32_t Apply(uint32_t raw, uint64_t milliseconds) {
        uint32_t result = 0;
        for (size_t i = 0; i < bindings_.size(); ++i) {
            if (!(raw & (1u << i))) continue;
            auto& binding = bindings_[i];
            const bool pressed = !(held_ & (1u << i));
            if (pressed) binding.started = milliseconds;
            if (binding.mode == 0 || (binding.mode == 2 && pressed) ||
                (binding.mode == 1 && (milliseconds - binding.started) % 100 < 50))
                result |= binding.mask;
        }
        held_ = raw;
        return result;
    }
};
} // namespace reagba
