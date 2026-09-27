#pragma once
#include "core/IEmulatorCore.h"
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
namespace reagba {
using Json = nlohmann::json;
namespace fs = std::filesystem;
std::vector<uint8_t> ReadBytes(const fs::path &, size_t maximum = 64 * 1024 * 1024);
std::string SHA256(const std::vector<uint8_t> &);
struct ROMInfo {
    fs::path path;
    std::string title, code, hash;
    size_t size = 0;
    bool headerChecksum = false;
    EmulatorSystem system = EmulatorSystem::GBA;
    Json ToJson() const;
};
ROMInfo InspectROM(const fs::path &, bool hash = true);
Json ScanROMs(const fs::path &);
} // namespace reagba
