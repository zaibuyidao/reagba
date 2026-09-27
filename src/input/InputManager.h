#pragma once
#include "rom/ROMManager.h"
#include "core/IEmulatorCore.h"
#include <SDL.h>
#include <array>
namespace reagba {
class InputManager {
    SDL_GameController *pad_ = nullptr;
    std::array<SDL_Scancode, 11> mapping_ = {
        SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN, SDL_SCANCODE_D,
        SDL_SCANCODE_A, SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_Q, SDL_SCANCODE_O, SDL_SCANCODE_L};
    std::array<bool, SDL_NUM_SCANCODES> held_{};
    std::array<bool, SDL_NUM_SCANCODES> pressed_{};
    bool fastForward_ = false;
    uint32_t lastScan_ = 0;

  public:
    // Takes ownership when a controller is supplied (used by virtual-device tests).
    explicit InputManager(SDL_GameController* pad = nullptr) : pad_(pad) {}
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
    bool FastForward() const { return fastForward_; }
    const std::array<SDL_Scancode, 11> &Mapping() const {
        return mapping_;
    }
};
} // namespace reagba
