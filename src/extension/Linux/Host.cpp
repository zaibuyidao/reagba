#include "extension/Host.h"
#include "reaper/ReaperAPI.h"
#include "swell/swell-dlggen.h"
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/wait.h>
#include <spawn.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cerrno>
#include "extension/Linux/SharedFrame.h"
#include <chrono>
#include <cstring>
#include <stdexcept>
extern char** environ;

// An empty WebView container has no dialog controls to scale. Register its
// pixel size directly; the deprecated resource macro uses an OSX-only flag.
static SWELL_DialogRegHelper reaGBADialog(&SWELL_curmodule_dialogresource_head,
    [](HWND, int) {}, 101, SWELL_DLG_WS_RESIZABLE, "ReaGBA", 760, 900, 1.0, 1.0);

namespace reagba {
namespace {
// Do not link GTK into REAPER: its SWELL build may use a different GDK ABI.
class LinuxHost final : public WebViewHost {
    HostConfig config_;
    FramePublisher frames_;
    GameViewport viewport_;
    VideoSettings video_;
    bool blocked_=false,active_=false;
    uint64_t keyEvents_=0;
    HWND window_ = nullptr;
    int socket_ = -1;
    pid_t child_ = -1;
    bool failed_ = false, ready_ = false;
    std::string input_,output_,geometry_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    static INT_PTR DialogProc(HWND window, UINT message, WPARAM, LPARAM lp) {
        // SWELL's WindowLong API uses LONG_PTR, including on 64-bit platforms.
        if (message == WM_INITDIALOG) { SetWindowLong(window,GWL_USERDATA,lp); return TRUE; }
        auto* self = reinterpret_cast<LinuxHost*>(GetWindowLong(window,GWL_USERDATA));
        if (self && message == WM_CLOSE) { if (self->config_.close) self->config_.close(); return TRUE; }
        if (self && message == WM_SETFOCUS) { self->Queue("{\"_host\":\"focus\"}"); return TRUE; }
        return FALSE;
    }
    void Fail(std::string error) { if (!failed_) { failed_ = true; config_.error(std::move(error)); } }
    void Queue(const std::string& text) {
        if (output_.size() + text.size() > 16*1024*1024) { Fail("WebKitGTK output queue overflow; reopen ReaGBA"); return; }
        output_ += text; output_ += '\n';
    }
    void Shutdown() {
        if (socket_ >= 0) { shutdown(socket_,SHUT_RDWR); close(socket_); socket_ = -1; }
        if (child_ > 0) {
            int status = 0;
            if (waitpid(child_,&status,WNOHANG) == 0) {
                // Shutdown path only: SIGTERM defaults to immediate exit in the helper.
                kill(child_,SIGTERM);
                while (waitpid(child_,&status,0) < 0 && errno == EINTR) {}
            }
            child_ = -1;
        }
        if (window_) { SetWindowLong(window_,GWL_USERDATA,0); DestroyWindow(window_); window_ = nullptr; }
    }
    void Geometry() {
        if (!ready_) return;
        HWND ancestor = window_;
        void* native = nullptr;
        while (ancestor && !(native = SWELL_GetOSWindow(ancestor,"GdkWindow"))) ancestor = GetParent(ancestor);
        if (!native) return;
        // Resolve from the GDK already used by SWELL; never cast HWND to GtkWidget.
        using GetXid = unsigned long (*)(void*);
        auto getXid = reinterpret_cast<GetXid>(dlsym(RTLD_DEFAULT,"gdk_x11_window_get_xid"));
        if (!getXid) getXid = reinterpret_cast<GetXid>(dlsym(RTLD_DEFAULT,"gdk_x11_drawable_get_xid"));
        if (!getXid) { Fail("ReaGBA Linux requires REAPER running on X11/XWayland (GDK X11)"); return; }
        const auto xid = getXid(native);
        if (!xid) { Fail("Cannot obtain the X11 dock window; native Wayland embedding is unsupported"); return; }
        RECT rect{}; GetClientRect(window_,&rect);
        POINT origin{0,0}; ClientToScreen(window_,&origin); ScreenToClient(ancestor,&origin);
        nlohmann::json geometry = {{"_host","geometry"},{"parent",xid},{"x",origin.x},{"y",origin.y},
            {"width",std::max(1L,static_cast<long>(rect.right-rect.left))},
            {"height",std::max(1L,static_cast<long>(rect.bottom-rect.top))},{"visible",IsWindowVisible(window_) != 0}};
        auto text = geometry.dump();
        if (text != geometry_) { geometry_ = text; Queue(text); }
    }
public:
    explicit LinuxHost(HostConfig config) : config_(std::move(config)) {
        if (!std::filesystem::is_regular_file(config_.helperPath)) throw std::runtime_error("Missing ReaGBA helper executable");
        // HTTP downloads (including ReaPack's script assets) may not retain the executable bit.
        if (access(config_.helperPath.c_str(), X_OK) != 0)
            std::filesystem::permissions(config_.helperPath, std::filesystem::perms::owner_exec,
                std::filesystem::perm_options::add);
        if (!getenv("DISPLAY")) throw std::runtime_error("ReaGBA Linux MVP requires an X11/XWayland DISPLAY");
        try {
            window_ = CreateDialogParam(nullptr,MAKEINTRESOURCE(101),static_cast<HWND>(config_.parent),DialogProc,reinterpret_cast<LPARAM>(this));
            if (!window_) throw std::runtime_error("Cannot create SWELL dock container");
            int pair[2];
            if (socketpair(AF_UNIX,SOCK_STREAM | SOCK_CLOEXEC,0,pair) != 0) throw std::runtime_error("Cannot create WebKitGTK IPC socket");
            socket_ = pair[0];
            const int socketSource=fcntl(pair[1],F_DUPFD_CLOEXEC,10),frameSource=fcntl(frames_.Descriptor(),F_DUPFD_CLOEXEC,10);
            if(socketSource<0||frameSource<0){if(socketSource>=0)close(socketSource);if(frameSource>=0)close(frameSource);close(pair[1]);throw std::runtime_error("Cannot duplicate helper descriptors");}
            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_addclose(&actions,pair[0]);
            posix_spawn_file_actions_adddup2(&actions,socketSource,3);
            if (pair[1] != 3) posix_spawn_file_actions_addclose(&actions,pair[1]);
            posix_spawn_file_actions_adddup2(&actions,frameSource,4);
            posix_spawn_file_actions_addclose(&actions,socketSource);
            posix_spawn_file_actions_addclose(&actions,frameSource);
            auto helper = config_.helperPath.string(), entry = (config_.uiDirectory / "index.html").string();
            char* args[] = {helper.data(),entry.data(),nullptr};
            const int error = posix_spawn(&child_,helper.c_str(),&actions,nullptr,args,environ);
            posix_spawn_file_actions_destroy(&actions); close(pair[1]);close(socketSource);close(frameSource);
            if (error) { child_ = -1; throw std::runtime_error(std::string("Cannot start WebKitGTK: ") + strerror(error)); }
            fcntl(socket_,F_SETFL,fcntl(socket_,F_GETFL) | O_NONBLOCK);
        } catch (...) { Shutdown(); throw; }
    }
    ~LinuxHost() override { Shutdown(); }
    void* Window() const override { return window_; }
    void SetTitle(const std::string& title) override { SetWindowText(window_,title.c_str()); }
    void Post(const std::string& json) override { if (ready_) Queue(json); }
    void SetViewport(GameViewport v) override {
        viewport_=v;Queue(nlohmann::json{{"_host","viewport"},{"x",v.x},{"y",v.y},{"width",v.width},{"height",v.height},{"clipTop",v.clipTop},{"clipBottom",v.clipBottom},{"clientWidth",v.clientWidth},{"visible",v.visible}}.dump());
    }
    void SetVideo(VideoSettings video) override {
        video_=video;nlohmann::json keys=nlohmann::json::array();for(auto scan:config_.input->Mapping())keys.push_back(SDL_GetScancodeName(scan));
        Queue(nlohmann::json{{"_host","settings"},{"integer",video.integerScaling},{"linear",video.linear},{"shader",ShaderPresetName(video.shader)},{"keys",keys}}.dump());
    }
    void FocusGame() override {blocked_=false;config_.input->Clear();Queue("{\"_host\":\"focus_game\"}");}
    void BlockKeyboard(bool blocked) override {blocked_=blocked;config_.input->Clear();Queue(nlohmann::json{{"_host","keyboard"},{"blocked",blocked}}.dump());}
    bool Active() const override {return active_&&IsWindowVisible(window_);}
    bool KeyboardBlocked() const override {return blocked_;}
    Json Diagnostics() const override {RECT r{};GetClientRect(window_,&r);return {{"active",Active()},{"keyboard_blocked",blocked_},{"key_events",keyEvents_},{"client_width",r.right},{"client_height",r.bottom}};}
    void Tick() override {
        if (failed_) return;
        int status = 0;
        if (child_ > 0 && waitpid(child_,&status,WNOHANG) == child_) {
            child_ = -1; Fail("WebKitGTK helper exited. Check GTK3/WebKitGTK 4.1 dependencies and X11 session."); return;
        }
        if (!ready_ && std::chrono::steady_clock::now() - started_ > std::chrono::seconds(15)) {
            Fail("Timed out starting WebKitGTK"); return;
        }
        // Finite reads/writes per REAPER timer; EAGAIN leaves data queued.
        char buffer[16384]; size_t total = 0;
        while (total < 65536) {
            const auto n = recv(socket_,buffer,sizeof(buffer),MSG_DONTWAIT);
            if (n < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) Fail("WebKitGTK IPC read failed"); break; }
            if (!n) { Fail("WebKitGTK IPC closed"); return; }
            input_.append(buffer,static_cast<size_t>(n)); total += static_cast<size_t>(n);
            size_t newline;
            while ((newline = input_.find('\n')) != std::string::npos) {
                auto line = input_.substr(0,newline); input_.erase(0,newline+1);
                if (line == "@READY") { ready_ = true; Geometry(); }
                else if (line.rfind("@ERROR ",0) == 0) Fail(line.substr(7));
                else if (line.rfind("@INPUT ",0)==0) {
                    auto event=nlohmann::json::parse(line.substr(7));active_=event.value("active",false);
                    if(!active_ || blocked_)config_.input->Clear();
                    else if(event.contains("key")){config_.input->Key(SDL_GetScancodeFromName(event.at("key").get<std::string>().c_str()),event.at("down").get<bool>());++keyEvents_;}
                }
                else if (!config_.receive(std::move(line))) Queue("{\"type\":\"error\",\"error\":\"Native queue busy; retry\"}");
            }
            if (input_.size() > 65536) { Fail("WebKitGTK message exceeds size limit"); return; }
        }
        Geometry();
        const bool active=Active()&&!blocked_;
        config_.manager->SetInput(config_.input->Poll(active));config_.manager->SetFastForward(config_.input->FastForward());
        if(config_.manager->frames.Consume())frames_.Publish(config_.manager->frames.Front());
        if (!output_.empty()) {
            const auto n = send(socket_,output_.data(),std::min<size_t>(65536,output_.size()),MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n > 0) output_.erase(0,static_cast<size_t>(n));
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) Fail("WebKitGTK IPC write failed");
        }
    }
};
}
std::unique_ptr<WebViewHost> CreateWebViewHost(HostConfig config) { return std::make_unique<LinuxHost>(std::move(config)); }
}
