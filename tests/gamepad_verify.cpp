#include "input/GamepadBindings.h"
#include "input/InputManager.h"
#include <iostream>
using namespace reagba;
static void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
int main() {
    try {
        GamepadBindings mapping;
        const auto defaults = DefaultGamepadBindings();
        require(NormalizeGamepadBindings(Json::object()) == defaults, "default configuration");
        require(mapping.Apply(1, 0) == 1 && mapping.Apply(1, 1000) == 1, "default A holds");
        require(mapping.Apply(0, 1001) == 0, "release");
        require(mapping.Apply((1u << 9) | (1u << 10) | (1u << 16), 1002) == ((1u << 9) | (1u << 8) | (1u << 6)), "default shoulders and stick");
        for (auto source : GamepadSources) {
            for (size_t target = 0; target < GamepadTargets.size(); ++target) {
                Json config = defaults;
                config[source] = {{"target", GamepadTargets[target]}, {"mode", "hold"}};
                mapping.Configure(config);
                const auto index = std::find(GamepadSources.begin(), GamepadSources.end(), source) - GamepadSources.begin();
                mapping.Apply(0, 0);
                require(mapping.Apply(1u << index, 0) == (1u << target), "every source can bind every target");
            }
        }
        auto config = defaults;
        config["x"] = {{"target", "a"}, {"mode", "single"}};
        config["y"] = {{"target", "b"}, {"mode", "turbo"}};
        config["a"] = {{"target", "none"}, {"mode", "hold"}};
        mapping.Configure(config);
        mapping.Apply(0, 0);
        require(NormalizeGamepadBindings(config)["x"] == Json({{"target", "a"}, {"mode", "hold"}}), "legacy single migrates without losing binding");
        require(mapping.Apply(4, 0) == 1, "standard press");
        require(mapping.Apply(4, 1) == 1 && mapping.Apply(4, 1000) == 1, "standard input stays held");
        mapping.Configure(config);
        require(mapping.Apply(4, 1001) == 1, "saving preserves standard hold");
        require(mapping.Apply(0, 1002) == 0, "standard release");
        require(mapping.Apply(4, 1003) == 1, "standard press after release");
        mapping.Apply(0, 1004);
        for (uint64_t time = 0; time < 1000; ++time)
            require(mapping.Apply(8, time) == (time % 100 < 50 ? 2u : 0u), "turbo 10 Hz with release phases");
        require(mapping.Apply(0, 1000) == 0 && mapping.Apply(8, 1001) == 2, "turbo stops and restarts immediately");
        require(mapping.Apply(8 | 2, 1055) == 2, "normal hold survives shared target turbo release");
        require(mapping.Apply(1, 1100) == 0, "unbound input");
        for (auto target : GamepadTargets) {
            config["button:40"] = {{"target", target}, {"mode", "turbo"}};
            const std::string action(target);
            const bool turbo = action == "a" || action == "b";
            require(NormalizeGamepadBindings(config)["button:40"]["mode"] == (turbo ? "turbo" : "hold"), "turbo is optional only for action targets");
        }
        for (Json invalid : {Json::array(), Json{{"unknown", {}}}, Json{{"a", {{"target", "invalid"}, {"mode", "hold"}}}},
                             Json{{"a", {{"target", "a"}, {"mode", "invalid"}}}}, Json{{"a", {{"target", 0}, {"mode", "hold"}}}}}) {
            bool rejected = false;
            try { NormalizeGamepadBindings(invalid); } catch (const std::exception&) { rejected = true; }
            require(rejected, "invalid configuration rejected");
        }
        SDL_SetMainReady();
        require(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0, "SDL initialization");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        const int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
        require(device >= 0 && SDL_IsGameController(device), "virtual controller");
        auto* joystick = SDL_JoystickOpen(device);
        require(joystick != nullptr, "virtual joystick open");
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
        SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
        {
            auto* controller = SDL_GameControllerOpen(device);
            require(controller != nullptr, "virtual game controller open");
            char* existing = SDL_GameControllerMapping(controller);
            require(existing != nullptr, "virtual mapping");
            const std::string fullMapping = std::string(existing) + ",touchpad:b" + std::to_string(SDL_CONTROLLER_BUTTON_TOUCHPAD) + ",";
            SDL_free(existing);
            require(SDL_GameControllerAddMapping(fullMapping.c_str()) >= 0, "virtual touchpad mapping");
            InputManager input(controller);
            require(input.PollGamepad(true) == 0, "initial neutral state");
            for (unsigned i = 0; i < 16; ++i) {
                const int button = i == 15 ? SDL_CONTROLLER_BUTTON_TOUCHPAD : i;
                SDL_JoystickSetVirtualButton(joystick, button, 1);
                const auto raw = input.PollGamepad(true);
                if (raw != (1u << i)) throw std::runtime_error("SDL button source mapping: " + std::to_string(i) + " received " + std::to_string(raw));
                require(input.PollGamepad(false) == 0, "inactive input released");
                SDL_JoystickSetVirtualButton(joystick, button, 0);
                require(input.PollGamepad(true) == 0, "SDL button release");
            }
            for (unsigned i = 0; i < 10; ++i) {
                const unsigned axis = i >= 8 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT + i - 8 : (i / 4) * 2 + (i % 4 < 2 ? 1 : 0);
                const int value = i >= 8 || i % 2 ? 32767 : -32768;
                SDL_JoystickSetVirtualAxis(joystick, axis, value);
                require(input.PollGamepad(true) == (1u << (16 + i)), "SDL stick or trigger source mapping");
                SDL_JoystickSetVirtualAxis(joystick, axis, i >= 8 ? -32768 : 0);
                require(input.PollGamepad(true) == 0, "SDL axis neutral");
            }
            SDL_JoystickSetVirtualButton(joystick, 0, 1);
            require(input.PollGamepad(true) == 1, "held before disconnect");
            require(input.GamepadState()["aliases"]["button:0"] == Json::array({"a"}), "capture reports standard alias for replacement");
            require(SDL_JoystickDetachVirtual(device) == 0, "virtual disconnect");
            require(input.PollGamepad(true) == 0, "disconnect releases input");
        }
        SDL_JoystickClose(joystick);
        const int generic = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 2, 48, 1);
        require(generic >= 0, "generic device");
        auto* rawJoystick = SDL_JoystickOpen(generic);
        require(rawJoystick != nullptr, "generic open");
        {
            InputManager input(nullptr, rawJoystick);
            SDL_JoystickSetVirtualButton(rawJoystick, 40, 1);
            SDL_JoystickSetVirtualAxis(rawJoystick, 1, -32768);
            SDL_JoystickSetVirtualHat(rawJoystick, 0, SDL_HAT_RIGHTUP);
            require(input.PollGamepad(true) == 0, "unmapped joystick has no standard bindings");
            const auto& raw = input.RawGamepadInputs();
            for (const auto* code : {"button:40", "axis:1:-", "hat:0:1", "hat:0:2"})
                require(std::find(raw.begin(), raw.end(), code) != raw.end(), "arbitrary raw input captured");
            auto config = defaults;
            config["button:40"] = {{"target", "a"}, {"mode", "hold"}};
            config["axis:1:-"] = {{"target", "l"}, {"mode", "hold"}};
            config["hat:0:2"] = {{"target", "b"}, {"mode", "turbo"}};
            mapping.Configure(config);mapping.Apply(0, 0);
            require(mapping.Apply(0, 1, raw) == (1 | 512 | 2), "arbitrary raw bindings reach GBA actions");
            require(mapping.Apply(0, 60, raw) == (1 | 512), "raw standard holds during turbo release phase");
            require(mapping.Apply(0, 61) == 0, "raw release");
            require(mapping.Apply(0, 62, raw) == (1 | 512 | 2), "raw turbo restarts on press");
            require(input.PollGamepad(false) == 0 && input.GamepadState()["connected"] == true, "capture works while gameplay inactive");
            SDL_JoystickDetachVirtual(generic);input.PollGamepad(true);
            require(input.RawGamepadInputs().empty() && input.GamepadState()["connected"] == false, "raw disconnect release");
        }
        SDL_Quit();
        std::cout << "PASS: gamepad defaults, remapping, modes, validation, SDL buttons/axes, focus and disconnect\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
