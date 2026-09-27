#pragma once
#include "rom/ROMManager.h"
#include "core/IEmulatorCore.h"
#include <SDL.h>
#include <array>
namespace reagba {
class InputManager {
    SDL_GameController *pad_ = nullptr;
    SDL_Joystick* joystick_ = nullptr;
    std::vector<std::string> rawInputs_;
    Json gamepadState_ = {{"connected", false}, {"inputs", Json::array()}, {"aliases", Json::object()}};
    std::array<SDL_Scancode, 11> mapping_ = {
        SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN, SDL_SCANCODE_D,
        SDL_SCANCODE_A, SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_Q, SDL_SCANCODE_O, SDL_SCANCODE_L};
    std::array<bool, SDL_NUM_SCANCODES> held_{};
    std::array<bool, SDL_NUM_SCANCODES> pressed_{};
    bool fastForward_ = false;
    uint32_t lastScan_ = 0;

  public:
    // Supplied SDL handles transfer ownership (used by virtual-device tests).
    explicit InputManager(SDL_GameController* pad = nullptr, SDL_Joystick* joystick = nullptr) : pad_(pad), joystick_(joystick) {}
    ~InputManager();
    void Configure(const Json &);
    void Key(SDL_Scancode key, bool down) {
        if (key > 0 && key < SDL_NUM_SCANCODES) {
            pressed_[key] = pressed_[key] || (down && !held_[key]);
            held_[key] = down;
        }
    }
    void Clear() {
        held_.fill(false);
        pressed_.fill(false);
        fastForward_ = false;
    }
    uint32_t Poll(bool active);
    uint32_t PollGamepad(bool active);
    const std::vector<std::string>& RawGamepadInputs() const { return rawInputs_; }
    const Json& GamepadState() const { return gamepadState_; }
    bool FastForward() const { return fastForward_; }
    const std::array<SDL_Scancode, 11> &Mapping() const {
        return mapping_;
    }
};
} // namespace reagba
