#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reagba {
constexpr int Width = 240, Height = 160, SampleRate = 32768;
constexpr double NativeFPS = 16777216.0 / 280896.0;
using Frame = std::array<uint32_t, Width * Height>; // byte order RGBA
enum class Button : unsigned { A, B, Select, Start, Right, Left, Up, Down, R, L };
enum class EmulatorSystem { GBA };
class IEmulatorCore {
  public:
    virtual ~IEmulatorCore() = default;
    virtual void LoadROM(const std::filesystem::path &path, const std::filesystem::path &bios = {}) = 0;
    virtual void Reset() = 0;
    virtual void RunFrame() = 0;
    virtual void SetInput(uint32_t keys) = 0;
    virtual uint32_t ReadInput() const = 0;
    virtual const Frame &GetFrame() const = 0;
    virtual std::vector<int16_t> DrainAudio() = 0;
    virtual std::vector<uint8_t> SaveState() = 0;
    virtual void LoadState(const std::vector<uint8_t> &state) = 0;
    virtual std::vector<uint8_t> SaveGame() = 0;
    virtual void LoadGameSave(const std::vector<uint8_t> &save) = 0;
    virtual std::string Version() const = 0;
    virtual EmulatorSystem GetSystem() const {
        return EmulatorSystem::GBA;
    }
};
// Scheduling and pause/start/stop belong to EmulatorManager, never to the UI or core.
} // namespace reagba
