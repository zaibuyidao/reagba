#include "save/SaveManager.h"
#include <fstream>
#include <ctime>
#ifdef _WIN32
#include <windows.h>
#endif
namespace reagba {
void AtomicWrite(const fs::path &path, const std::vector<uint8_t> &bytes) {
    fs::create_directories(path.parent_path());
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) || !file.flush())
            throw std::runtime_error("Write failed: " + temp.u8string());
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Could not replace save file");
#else
    fs::rename(temp, path);
#endif
}
void WriteJSON(const fs::path &path, const Json &j) {
    auto s = j.dump(2);
    AtomicWrite(path, {s.begin(), s.end()});
}
void WriteScreenshot(const fs::path &path, const Frame &frame, EmulatorSystem system) {
    // Uncompressed 24-bit BMP is portable and contains no proprietary metadata.
    const int width = system == EmulatorSystem::GBA ? Width : 160;
    const int height = system == EmulatorSystem::GBA ? Height : 144;
    const int left = (Width - width) / 2, top = (Height - height) / 2;
    std::vector<uint8_t> out(54 + width * height * 3, 0);
    auto u32 = [&](size_t p, uint32_t n) {
        for (int i = 0; i < 4; ++i)
            out[p + i] = uint8_t(n >> (8 * i));
    };
    out[0] = 'B';
    out[1] = 'M';
    u32(2, uint32_t(out.size()));
    u32(10, 54);
    u32(14, 40);
    u32(18, width);
    u32(22, height);
    out[26] = 1;
    out[28] = 24;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            uint32_t p = frame[(top + height - 1 - y) * Width + left + x];
            size_t i = 54 + (y * width + x) * 3;
            out[i] = uint8_t(p >> 16);
            out[i + 1] = uint8_t(p >> 8);
            out[i + 2] = uint8_t(p);
        }
    AtomicWrite(path, out);
}
SaveManager::SaveManager(fs::path root) : root_(std::move(root)) {
    for (auto dir : {"saves", "states", "screenshots", "config", "cache"})
        fs::create_directories(root_ / dir);
}
static void CheckSlot(int slot) {
    if (slot < 1 || slot > 9)
        throw std::runtime_error("Save slot must be 1–9");
}
void SaveManager::SaveBattery(IEmulatorCore &core, const ROMInfo &rom) {
    auto bytes = core.SaveGame();
    if (!bytes.empty())
        AtomicWrite(root_ / "saves" / (rom.hash + ".sav"), bytes);
}
void SaveManager::LoadBattery(IEmulatorCore &core, const ROMInfo &rom) {
    auto path = root_ / "saves" / (rom.hash + ".sav");
    if (fs::exists(path))
        core.LoadGameSave(ReadBytes(path, 1024 * 1024));
}
void SaveManager::SaveSlot(IEmulatorCore &core, const ROMInfo &rom, int slot) {
    CheckSlot(slot);
    auto payload = core.SaveState();
    Json meta = {{"format", 1},
                 {"system", SystemName(rom.system)},
                 {"core", core.Version()},
                 {"rom_sha256", rom.hash},
                 {"timestamp", std::time(nullptr)},
                 {"payload_sha256", SHA256(payload)}};
    auto header = meta.dump();
    std::vector<uint8_t> bytes = {'R', 'G', 'B', 'A', 'S', 'T', '0', '1'};
    uint32_t size = uint32_t(header.size());
    for (int i = 0; i < 4; ++i)
        bytes.push_back(uint8_t(size >> (8 * i)));
    bytes.insert(bytes.end(), header.begin(), header.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    auto base = root_ / "states" / (rom.hash + "-" + std::to_string(slot));
    auto state = base;
    state += ".state";
    AtomicWrite(state, bytes);
    auto picture = base;
    picture += ".bmp";
    WriteScreenshot(picture, core.GetFrame(), core.GetSystem());
    auto json = base;
    json += ".json";
    WriteJSON(json, meta);
}
void SaveManager::LoadSlot(IEmulatorCore &core, const ROMInfo &rom, int slot) {
    CheckSlot(slot);
    auto bytes =
        ReadBytes(root_ / "states" / (rom.hash + "-" + std::to_string(slot) + ".state"), 16 * 1024 * 1024);
    if (bytes.size() < 12 || std::string(bytes.begin(), bytes.begin() + 8) != "RGBAST01")
        throw std::runtime_error("Invalid save-state envelope");
    uint32_t n = 0;
    for (int i = 0; i < 4; ++i)
        n |= uint32_t(bytes[8 + i]) << (8 * i);
    if (n > 65536 || 12ull + n >= bytes.size())
        throw std::runtime_error("Invalid save-state metadata size");
    auto meta = Json::parse(bytes.begin() + 12, bytes.begin() + 12 + n);
    if (meta.at("format") != 1 || meta.at("system") != SystemName(rom.system) || meta.at("core") != core.Version() ||
        meta.at("rom_sha256") != rom.hash)
        throw std::runtime_error("Save state belongs to a different ROM or core version");
    std::vector<uint8_t> payload(bytes.begin() + 12 + n, bytes.end());
    if (meta.at("payload_sha256") != SHA256(payload))
        throw std::runtime_error("Save state checksum mismatch");
    core.LoadState(payload);
}
Json SaveManager::Slots(const ROMInfo &rom) const {
    Json list = Json::array();
    for (int i = 1; i <= 9; ++i) {
        auto path = root_ / "states" / (rom.hash + "-" + std::to_string(i) + ".state");
        Json row = {{"slot", i}, {"exists", fs::exists(path)}};
        auto meta = path;
        meta.replace_extension(".json");
        if (fs::exists(meta)) {
            try {
                auto bytes = ReadBytes(meta, 65536);
                row["metadata"] = Json::parse(bytes);
            } catch (const std::exception &) {
            }
        }
        list.push_back(row);
    }
    return list;
}
} // namespace reagba
