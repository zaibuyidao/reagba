#include "rom/CoverManager.h"
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace reagba {
CoverDownload DownloadCoverFile(const std::string &path, size_t maximum, const CoverCancel &cancel) {
    if (path.empty() || path.front() != '/' || cancel())
        throw std::runtime_error("Cover download cancelled");
    CoverDownload out;
#ifdef _WIN32
    struct Handle {
        HINTERNET value;
        ~Handle() { if (value) WinHttpCloseHandle(value); }
    };
    Handle session{WinHttpOpen(L"ReaGBA/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) throw std::runtime_error("Cannot initialize cover download");
    WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 3000);
    Handle connection{WinHttpConnect(session.value, L"raw.githubusercontent.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    const std::wstring wide(path.begin(), path.end()); // paths are percent-encoded ASCII
    Handle request{WinHttpOpenRequest(connection.value, L"GET", wide.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) throw std::runtime_error("Cannot create cover request");
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    if (cancel() || !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr))
        throw std::runtime_error("Cover server unavailable");
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Invalid cover response");
    out.status = int(status);
    if (status != 200) return out;
    uint8_t buffer[16384];
    for (;;) {
        if (cancel() || std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Cover download cancelled or timed out");
        DWORD count = 0;
        if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &count))
            throw std::runtime_error("Incomplete cover download");
        if (!count) break;
        if (count > maximum - out.bytes.size()) throw std::runtime_error("Cover file too large");
        out.bytes.insert(out.bytes.end(), buffer, buffer + count);
    }
#else
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) throw std::runtime_error("Cannot initialize cover download");
    struct Handle { CURL *value = curl_easy_init(); ~Handle() { if (value) curl_easy_cleanup(value); } } handle;
    if (!handle.value) throw std::runtime_error("Cannot create cover request");
    struct Context { CoverDownload *out; size_t maximum; const CoverCancel *cancel; } context{&out, maximum, &cancel};
    const auto url = "https://raw.githubusercontent.com" + path;
    curl_easy_setopt(handle.value, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.value, CURLOPT_USERAGENT, "ReaGBA/1.0");
    curl_easy_setopt(handle.value, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.value, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(handle.value, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(handle.value, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle.value, CURLOPT_XFERINFODATA, &context);
    curl_easy_setopt(handle.value, CURLOPT_XFERINFOFUNCTION,
        +[](void *p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            return (*static_cast<Context *>(p)->cancel)() ? 1 : 0;
        });
    curl_easy_setopt(handle.value, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(handle.value, CURLOPT_WRITEFUNCTION,
        +[](char *data, size_t size, size_t count, void *p) -> size_t {
            auto &c = *static_cast<Context *>(p);
            const auto length = size * count;
            if ((*c.cancel)() || length > c.maximum - c.out->bytes.size()) return 0;
            try { c.out->bytes.insert(c.out->bytes.end(), data, data + length); }
            catch (...) { return 0; }
            return length;
        });
    if (curl_easy_perform(handle.value) != CURLE_OK) throw std::runtime_error("Cover server unavailable");
    long status = 0;
    curl_easy_getinfo(handle.value, CURLINFO_RESPONSE_CODE, &status);
    out.status = int(status);
#endif
    return out;
}
} // namespace reagba
