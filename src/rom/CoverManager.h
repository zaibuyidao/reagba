#pragma once
#include "rom/ROMManager.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace reagba {
struct CoverDownload {
    int status = 0;
    std::vector<uint8_t> bytes;
};
using CoverCancel = std::function<bool()>;
using CoverFetch = std::function<CoverDownload(const std::string &, size_t, const CoverCancel &)>;
// Only paths on raw.githubusercontent.com are accepted; no ROM data is sent.
CoverDownload DownloadCoverFile(const std::string &, size_t, const CoverCancel &);
bool ValidGameCode(const std::string &);

class CoverManager {
  public:
    explicit CoverManager(fs::path cache, CoverFetch fetch = DownloadCoverFile);
    ~CoverManager();
    void Enable(bool);
    void Shutdown();
    // Non-blocking: pending until the IO worker has loaded/downloaded the image.
    // Terminal results are consumed, keeping native image memory bounded.
    Json Get(const std::string &code);

  private:
    fs::path cache_;
    CoverFetch fetch_;
    std::atomic<bool> enabled_{false}, stopped_{false};
    std::atomic<unsigned> generation_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::map<std::string, Json> results_;
    std::map<std::string, std::vector<std::string>> names_;
    bool indexLoaded_ = false;
    std::chrono::steady_clock::time_point indexRetry_{};
    std::thread worker_;
    void Run();
    void LoadIndex(const CoverCancel &);
    Json Resolve(const std::string &, const CoverCancel &);
};
} // namespace reagba
