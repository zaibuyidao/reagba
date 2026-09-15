#pragma once
#include "app/EmulatorManager.h"
#include "input/InputManager.h"
#include "video/VideoTypes.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace reagba {
struct HostConfig {
    void* parent = nullptr;
    std::filesystem::path uiDirectory, dataDirectory, helperPath;
    EmulatorManager* manager = nullptr;
    InputManager* input = nullptr;
    std::function<bool(std::string)> receive;
    std::function<void()> close;
    std::function<void(std::string)> error;
};
class WebViewHost {
public:
    virtual ~WebViewHost() = default;
    virtual void* Window() const = 0;
    virtual void SetTitle(const std::string&) = 0;
    virtual void Post(const std::string&) = 0;
    virtual void Tick() = 0;
    virtual void SetViewport(GameViewport) = 0;
    virtual void SetVideo(VideoSettings) = 0;
    virtual void FocusGame() = 0;
    virtual void BlockKeyboard(bool) = 0;
    virtual bool Active() const = 0;
    virtual bool KeyboardBlocked() const = 0;
    virtual Json Diagnostics() const = 0;
};
std::unique_ptr<WebViewHost> CreateWebViewHost(HostConfig);
}
