#include "rom/CoverManager.h"
#include "save/SaveManager.h"
#include <algorithm>
#include <ctime>
#include <sstream>
#include <stdexcept>

namespace reagba {
namespace {
constexpr size_t MaxImage = 1024 * 1024, MaxIndex = 8 * 1024 * 1024;
constexpr const char *IndexPath = "/libretro/libretro-database/master/metadat/no-intro/Nintendo%20-%20Game%20Boy%20Advance.dat";
bool ValidPNG(const std::vector<uint8_t> &b) {
    static const uint8_t signature[] = {137,80,78,71,13,10,26,10};
    static const uint8_t ending[] = {0,0,0,0,73,69,78,68,174,66,96,130};
    if (b.size() < 57 || b.size() > MaxImage || !std::equal(signature, signature + 8, b.begin()) ||
        !std::equal(ending, ending + 12, b.end() - 12) ||
        std::string(b.begin() + 12, b.begin() + 16) != "IHDR") return false;
    auto u32 = [&](size_t p) { return (uint32_t(b[p]) << 24) | (uint32_t(b[p+1]) << 16) |
                                      (uint32_t(b[p+2]) << 8) | b[p+3]; };
    return u32(16) > 0 && u32(20) > 0 && u32(16) <= 2048 && u32(20) <= 2048;
}
std::string DataURI(const std::vector<uint8_t> &bytes) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = "data:image/png;base64,";
    out.reserve(out.size() + (bytes.size()+2)/3*4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t n = uint32_t(bytes[i]) << 16;
        if (i+1 < bytes.size()) n |= uint32_t(bytes[i+1]) << 8;
        if (i+2 < bytes.size()) n |= bytes[i+2];
        out += alphabet[n >> 18]; out += alphabet[(n >> 12) & 63];
        out += i+1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
        out += i+2 < bytes.size() ? alphabet[n & 63] : '=';
    }
    return out;
}
std::string ImagePath(std::string name) {
    for (auto &c : name) if (std::string("&*/:`<>?\\|\"").find(c) != std::string::npos) c = '_';
    const char *hex = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char c : name + ".png") {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') encoded += char(c);
        else { encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 15]; }
    }
    return "/libretro-thumbnails/Nintendo_-_Game_Boy_Advance/master/Named_Boxarts/" + encoded;
}
bool Fresh(const fs::path &path, int days) {
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    return !ec && fs::file_time_type::clock::now() - time < std::chrono::hours(24 * days);
}
std::map<std::string, std::vector<std::string>> ParseIndex(const std::vector<uint8_t> &bytes) {
    std::map<std::string, std::vector<std::string>> names;
    std::istringstream stream(std::string(bytes.begin(), bytes.end()));
    std::string line, name;
    while (std::getline(stream, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        line.erase(0, first);
        if (line.rfind("game (", 0) == 0) name.clear();
        auto quoted = [&] {
            const auto begin = line.find('"'), end = line.rfind('"');
            return begin != std::string::npos && end > begin ? line.substr(begin + 1, end - begin - 1) : std::string();
        };
        if (line.rfind("name \"", 0) == 0) name = quoted();
        if (line.rfind("serial \"", 0) == 0 && !name.empty() && name.size() <= 240) {
            const auto code = quoted();
            if (!ValidGameCode(code)) continue;
            auto &list = names[code];
            if (list.size() < 4 && std::find(list.begin(), list.end(), name) == list.end()) list.push_back(name);
        }
    }
    return names;
}
}
bool ValidGameCode(const std::string &code) {
    return code.size() == 4 && std::all_of(code.begin(), code.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    });
}
CoverManager::CoverManager(fs::path cache, CoverFetch fetch) : cache_(std::move(cache)), fetch_(std::move(fetch)) {
    worker_ = std::thread(&CoverManager::Run, this);
}
CoverManager::~CoverManager() { Shutdown(); }
void CoverManager::Shutdown() {
    stopped_ = true; cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}
void CoverManager::Enable(bool value) {
    if (enabled_.exchange(value) != value) ++generation_;
}
Json CoverManager::Get(const std::string &code) {
    if (!ValidGameCode(code)) return {{"status", "missing"}};
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = results_.find(code);
    if (it != results_.end()) {
        auto result = it->second;
        if (result.at("status") != "pending") results_.erase(it);
        return result;
    }
    if (results_.size() >= 32) {
        const auto completed = std::find_if(results_.begin(), results_.end(), [](const auto &entry) {
            return entry.second.at("status") != "pending";
        });
        if (completed != results_.end()) results_.erase(completed);
    }
    if (results_.size() >= 32 || stopped_) return {{"status", "busy"}};
    results_[code] = {{"status", "pending"}};
    queue_.push_back(code); cv_.notify_one();
    return {{"status", "pending"}};
}
void CoverManager::LoadIndex(const CoverCancel &cancel) {
    if (indexLoaded_) return;
    const auto file = cache_ / "gba-index.dat";
    std::vector<uint8_t> bytes;
    try { bytes = ReadBytes(file, MaxIndex); } catch (...) {}
    auto names = ParseIndex(bytes);
    if ((names.empty() || !Fresh(file, 30)) && std::chrono::steady_clock::now() >= indexRetry_) {
        indexRetry_ = std::chrono::steady_clock::now() + std::chrono::minutes(10);
        try {
            const auto result = fetch_(IndexPath, MaxIndex, cancel);
            if (result.status == 200 && result.bytes.size() <= MaxIndex && !cancel()) {
                auto downloaded = ParseIndex(result.bytes);
                if (!downloaded.empty()) {
                    AtomicWrite(file, result.bytes); names = std::move(downloaded);
                }
            }
        } catch (...) { if (cancel()) { indexRetry_ = {}; throw; } }
    }
    if (cancel()) { indexRetry_ = {}; throw std::runtime_error("Cover download cancelled"); }
    if (names.empty()) throw std::runtime_error("Cover index unavailable");
    names_ = std::move(names);
    indexLoaded_ = true;
}
Json CoverManager::Resolve(const std::string &code, const CoverCancel &cancel) {
    const auto image = cache_ / (code + ".png"), failure = cache_ / (code + ".json");
    try {
        const auto bytes = ReadBytes(image, MaxImage);
        if (ValidPNG(bytes)) return {{"status", "ready"}, {"image", DataURI(bytes)}};
    } catch (...) {}
    if (cancel()) return {{"status", "disabled"}};
    try {
        const auto previous = Json::parse(ReadBytes(failure, 4096));
        const auto status = previous.value("status", "error");
        if ((status == "missing" || status == "error") && previous.value("retry_after", int64_t(0)) > std::time(nullptr))
            return {{"status", status}};
    } catch (...) {}
    auto remember = [&](const char *status, int seconds) {
        WriteJSON(failure, {{"status", status}, {"retry_after", std::time(nullptr) + seconds}});
        return Json{{"status", status}};
    };
    try {
        LoadIndex(cancel);
        const auto it = names_.find(code);
        if (it != names_.end()) for (const auto &name : it->second) {
            if (cancel()) return {{"status", "disabled"}};
            const auto result = fetch_(ImagePath(name), MaxImage, cancel);
            if (cancel()) return {{"status", "disabled"}};
            if (result.status == 404) continue;
            if (result.status != 200 || !ValidPNG(result.bytes)) throw std::runtime_error("Invalid cover image");
            AtomicWrite(image, result.bytes);
            return {{"status", "ready"}, {"image", DataURI(result.bytes)}};
        }
        return remember("missing", 7 * 24 * 3600);
    } catch (...) {
        if (cancel()) return {{"status", "disabled"}};
        return remember("error", 10 * 60);
    }
}
void CoverManager::Run() {
    for (;;) {
        std::string code;
        unsigned generation;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return stopped_ || !queue_.empty(); });
            if (stopped_) return;
            code = queue_.front(); queue_.pop_front(); generation = generation_;
        }
        Json result;
        try { result = Resolve(code, [&] { return stopped_ || !enabled_ || generation_ != generation; }); }
        catch (...) { result = {{"status", "error"}}; }
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation_ != generation && result.at("status") != "ready") {
            queue_.push_back(code);
            continue;
        }
        results_[code] = std::move(result);
    }
}
} // namespace reagba
