#pragma once
#include "rom/ROMManager.h"
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <regex>

namespace reagba {
// Source bits are independent of SDL button indices and shared with the settings UI.
inline constexpr std::array<const char*, 26> GamepadSources = {
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
    "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "touchpad",
    "leftup", "leftdown", "leftleft", "leftright", "rightup", "rightdown", "rightleft", "rightright",
    "lefttrigger", "righttrigger"};
inline constexpr std::array<const char*, 11> GamepadTargets = {
    "a", "b", "select", "start", "right", "left", "up", "down", "r", "l", "fast_forward"};
inline constexpr uint32_t GamepadFastForwardMask = 1u << 10;
inline Json DefaultGamepadBindings() {
    static constexpr int targets[] = {0, 1, -1, -1, 2, -1, 3, -1, -1, 9, 8, 6, 7, 5, 4, -1,
                                       6, 7, 5, 4, -1, -1, -1, -1, -1, -1};
    Json result = Json::object();
    for (size_t i = 0; i < GamepadSources.size(); ++i)
        result[GamepadSources[i]] = {{"target", targets[i] < 0 ? "none" : GamepadTargets[targets[i]]}, {"mode", "hold"}};
    return result;
}
inline Json NormalizeGamepadBindings(const Json& value) {
    if (!value.is_object() || value.size() > 1024) throw std::runtime_error("Gamepad bindings must be an object");
    auto result = DefaultGamepadBindings();
    for (auto it = value.begin(); it != value.end(); ++it) {
        static const std::regex rawSource("(button:[0-9]{1,5}|axis:[0-9]{1,5}:[+-]|hat:[0-9]{1,5}:[1248])");
        if (!result.contains(it.key()) && !std::regex_match(it.key(), rawSource))
            throw std::runtime_error("Unknown gamepad input");
        const auto& binding = it.value();
        if (!binding.is_object() || binding.size() != 2 || !binding.contains("target") || !binding.contains("mode"))
            throw std::runtime_error("Invalid gamepad binding");
        bool valid = binding["target"] == "none";
        for (auto target : GamepadTargets) valid |= binding["target"] == target;
        if (!valid) throw std::runtime_error("Unknown gamepad target");
        if (binding["mode"] != "hold" && binding["mode"] != "turbo" && binding["mode"] != "single")
            throw std::runtime_error("Unknown gamepad trigger mode");
        result[it.key()] = binding;
        // Legacy single-press bindings retain their input and target, using standard behavior.
        if (binding["mode"] == "single" || (binding["target"] != "a" && binding["target"] != "b"))
            result[it.key()]["mode"] = "hold";
    }
    return result;
}

class GamepadBindings {
    struct Binding { uint32_t mask = 0; int mode = 0; uint64_t started = 0; };
    std::map<std::string, Binding> bindings_;
    std::set<std::string> held_;
  public:
    GamepadBindings() { Configure(DefaultGamepadBindings()); }
    void Configure(const Json& config) {
        const auto normalized = NormalizeGamepadBindings(config);
        auto previous = std::move(bindings_);
        bindings_.clear();
        for (auto it = normalized.begin(); it != normalized.end(); ++it) {
            const auto& value = it.value();
            auto& binding = bindings_[it.key()];
            if (previous.count(it.key())) binding.started = previous[it.key()].started;
            for (size_t j = 0; j < GamepadTargets.size(); ++j)
                if (value["target"] == GamepadTargets[j]) binding.mask = 1u << j;
            binding.mode = value["mode"] == "turbo" ? 1 : 0;
        }
    }
    // Evaluate on the emulation thread using real time for turbo cadence.
    uint32_t Apply(uint32_t raw, uint64_t milliseconds, const std::vector<std::string>& inputs = {}) {
        std::set<std::string> current(inputs.begin(), inputs.end());
        for (size_t i = 0; i < GamepadSources.size(); ++i)
            if (raw & (1u << i)) current.insert(GamepadSources[i]);
        uint32_t result = 0;
        for (auto& entry : bindings_) {
            if (!current.count(entry.first)) continue;
            auto& binding = entry.second;
            const bool pressed = !held_.count(entry.first);
            if (pressed) binding.started = milliseconds;
            if (binding.mode == 0 || (milliseconds - binding.started) % 100 < 50)
                result |= binding.mask;
        }
        held_ = std::move(current);
        return result;
    }
};
} // namespace reagba
