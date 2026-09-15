#pragma once
#include "core/IEmulatorCore.h"
#include <memory>
namespace reagba {
class GBACore final : public IEmulatorCore {
  public:
    GBACore();
    ~GBACore() override;
    void LoadROM(const std::filesystem::path &, const std::filesystem::path & = {}) override;
    void Reset() override;
    void RunFrame() override;
    void SetInput(uint32_t) override;
    uint32_t ReadInput() const override;
    const Frame &GetFrame() const override;
    std::vector<int16_t> DrainAudio() override;
    std::vector<uint8_t> SaveState() override;
    void LoadState(const std::vector<uint8_t> &) override;
    std::vector<uint8_t> SaveGame() override;
    void LoadGameSave(const std::vector<uint8_t> &) override;
    std::string Version() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace reagba
