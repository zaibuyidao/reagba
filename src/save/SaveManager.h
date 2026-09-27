#pragma once
#include "core/IEmulatorCore.h"
#include "rom/ROMManager.h"
namespace reagba {
void AtomicWrite(const fs::path &, const std::vector<uint8_t> &);
void WriteJSON(const fs::path &, const Json &);
void WriteScreenshot(const fs::path &, const Frame &, EmulatorSystem = EmulatorSystem::GBA);
class SaveManager {
    fs::path root_;

  public:
    explicit SaveManager(fs::path root);
    const fs::path &Root() const {
        return root_;
    }
    void SaveBattery(IEmulatorCore &, const ROMInfo &);
    void LoadBattery(IEmulatorCore &, const ROMInfo &);
    void SaveSlot(IEmulatorCore &, const ROMInfo &, int slot);
    void LoadSlot(IEmulatorCore &, const ROMInfo &, int slot);
    Json Slots(const ROMInfo &) const;
};
} // namespace reagba
