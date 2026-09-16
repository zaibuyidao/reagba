#pragma once
#include <nlohmann/json.hpp>

namespace reagba {
struct WindowState {
    int x = 100, y = 100, width = 760, height = 900, dockId = -1;
    bool docked = false, maximized = false;

    void Restore(const nlohmann::json& settings) {
        if (!settings.is_object()) return;
        if (settings.contains("docked") && settings["docked"].is_boolean())
            docked = settings["docked"].get<bool>();
        if (settings.contains("maximized") && settings["maximized"].is_boolean())
            maximized = settings["maximized"].get<bool>();
        auto integer = [&](const char* key, int low, int high, int& target) {
            if (!settings.contains(key) || !settings[key].is_number_integer()) return;
            const auto value = settings[key].get<double>();
            if (value >= low && value <= high) target = static_cast<int>(value);
        };
        integer("x", -1000000, 1000000, x);
        integer("y", -1000000, 1000000, y);
        integer("width", 100, 100000, width);
        integer("height", 100, 100000, height);
        integer("dock_id", 0, 15, dockId);
    }
    nlohmann::json ToJson() const {
        return {{"x",x},{"y",y},{"width",width},{"height",height},
                {"docked",docked},{"dock_id",dockId},{"maximized",maximized}};
    }
};
}
