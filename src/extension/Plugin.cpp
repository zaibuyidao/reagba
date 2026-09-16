#define REAPERAPI_IMPLEMENT
#include "reaper/ReaperAPI.h"
#include "extension/Host.h"
#include "extension/RuntimePaths.h"
#include "extension/WindowState.h"
#include "audio/AudioEngine.h"
#include "bridge/WebBridge.h"
#include <deque>
#include <chrono>
#include <cstdlib>
#include <cmath>

namespace {
using namespace reagba;
reaper_plugin_info_t* plugin = nullptr;
using GetUserFileNameAPI = bool (*)(int,const char*,const char*,const char*,char*,int);
GetUserFileNameAPI getUserFileName = nullptr;
gaccel_register_t accelerator{};
int showCommand = 0;
bool closing = false, inTimer = false;
std::string pendingError;
struct Inbox {
    std::mutex mutex;
    bool alive = true;
    std::deque<Json> replies;
    void Put(Json reply) { std::lock_guard<std::mutex> lock(mutex); if(alive && replies.size()<256)replies.push_back(std::move(reply)); }
};
struct Session {
    bool sdl = false, statusPending = false;
    fs::path root, data, testDirectory;
    fs::path lastRomDirectory;
    std::unique_ptr<EmulatorManager> manager;
    std::unique_ptr<AudioEngine> audio;
    std::unique_ptr<InputManager> input;
    std::unique_ptr<WebBridge> bridge;
    std::unique_ptr<WebViewHost> host;
    std::shared_ptr<Inbox> inbox = std::make_shared<Inbox>();
    std::deque<Json> requests;
    Json lastStatus, lastLayout, testLog = Json::array(), testStates = Json::array();
    std::string lastTestRequest;
    std::chrono::steady_clock::time_point lastState{};
    WindowState windowState;
    Json savedWindowState, observedWindowState;
    std::chrono::steady_clock::time_point windowChanged{};
    ~Session() {
        { std::lock_guard<std::mutex> lock(inbox->mutex); inbox->alive=false; }
        host.reset();
        if(manager)manager->Shutdown();
        audio.reset();bridge.reset();manager.reset();input.reset();
        if(sdl)SDL_QuitSubSystem(SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER);
    }
};
std::unique_ptr<Session> session;
bool IsDocked();
void CaptureFloatingWindow() {
    const auto window=static_cast<HWND>(session->host->Window());
    auto& state=session->windowState;
    RECT rect{};
    if(!GetWindowRect(window,&rect))return;
#ifdef _WIN32
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    if(GetWindowPlacement(window,&placement)) {
        state.maximized=IsZoomed(window) || (IsIconic(window) && (placement.flags&WPF_RESTORETOMAXIMIZED));
        if(IsIconic(window)||IsZoomed(window)) {
            rect=placement.rcNormalPosition;
            // WINDOWPLACEMENT uses workspace coordinates for top-level windows.
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if(GetMonitorInfo(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))
                OffsetRect(&rect,monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top);
        }
    }
#endif
    if(rect.right<=rect.left || rect.bottom<=rect.top)return;
    state.x=rect.left;state.y=rect.top;state.width=rect.right-rect.left;state.height=rect.bottom-rect.top;
}
void PersistWindowState(bool immediate=false, bool capture=true) noexcept {
    try {
        if(!session || !session->host)return;
        auto& s=*session;
        const auto window=static_cast<HWND>(s.host->Window());
        if(capture && window && IsWindow(window)) {
            bool floating=false;
            const int dock=DockIsChildOfDock(window,&floating);
            s.windowState.docked=dock>=0;
            if(dock>=0)s.windowState.dockId=dock;else CaptureFloatingWindow();
        }
        const auto current=s.windowState.ToJson();
        const auto now=std::chrono::steady_clock::now();
        if(current!=s.observedWindowState) {s.observedWindowState=current;s.windowChanged=now;}
        if(current!=s.savedWindowState && (immediate || now-s.windowChanged>=std::chrono::milliseconds(500))) {
            WriteJSON(s.data/"config"/"window.json",current);
            if(s.windowState.docked && s.windowState.dockId>=0)Dock_UpdateDockID("ReaGBANativeDock",s.windowState.dockId);
            s.savedWindowState=current;
        }
    }catch(...){}
}
bool IsDocked() {
    bool floating=false;
    return session && session->host && DockIsChildOfDock(static_cast<HWND>(session->host->Window()),&floating)>=0;
}
void ShowFloating() {
    const auto window=static_cast<HWND>(session->host->Window());
    auto& state=session->windowState;
    SetParent(window,nullptr);
#ifdef _WIN32
    SetWindowLongPtr(window,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN);
    SetWindowLongPtr(window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(plugin->hwnd_main));
    RECT rect{state.x,state.y,state.x+state.width,state.y+state.height};
    if(!MonitorFromRect(&rect,MONITOR_DEFAULTTONULL)) {
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if(GetMonitorInfo(MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&monitor)) {
            state.x=monitor.rcWork.left;state.y=monitor.rcWork.top;
        }
    }
#endif
    SetWindowPos(window,nullptr,state.x,state.y,state.width,state.height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
#ifdef _WIN32
    ShowWindow(window,state.maximized?SW_SHOWMAXIMIZED:SW_SHOWNORMAL);
#else
    ShowWindow(window,SW_SHOW);
#endif
    SetForegroundWindow(window);
}
void SetDocked(bool dock) {
    const auto window=static_cast<HWND>(session->host->Window());
    if(dock!=IsDocked()) {
        if(dock) {
            CaptureFloatingWindow();
            if(session->windowState.dockId>=0)Dock_UpdateDockID("ReaGBANativeDock",session->windowState.dockId);
            DockWindowAddEx(window,"ReaGBA","ReaGBANativeDock",true);DockWindowActivate(window);
        }
        else {bool floating=false;const int index=DockIsChildOfDock(window,&floating);if(index>=0){session->windowState.dockId=index;Dock_UpdateDockID("ReaGBANativeDock",index);}DockWindowRemove(window);ShowFloating();}
    }
    PersistWindowState(true);
}
void Close(bool hostShutdown=false) {
    closing=false;
    if(session && session->host) {
        // REAPER may already have destroyed the Docker and its child windows
        // before unloading the extension. Keep the last live placement then.
        PersistWindowState(true,!hostShutdown);
        if(IsDocked())DockWindowRemove(static_cast<HWND>(session->host->Window()));
    }
    session.reset();
}
void Open() {
    if(session) {
        if(IsDocked())DockWindowActivate(static_cast<HWND>(session->host->Window()));
        else {ShowWindow(static_cast<HWND>(session->host->Window()),SW_RESTORE);SetForegroundWindow(static_cast<HWND>(session->host->Window()));}
        return;
    }
    auto next=std::make_unique<Session>();
    const auto paths=ResolveRuntimePaths(fs::u8path(GetResourcePath()));
    next->root=paths.product;
    const auto ui=paths.web;
    for(const auto* name:{"index.html","style.css","app.js"})
        if(!fs::is_regular_file(ui/name))throw std::runtime_error("Missing Scripts/zaibuyidao Scripts/ReaGBA/web UI files. Install the complete ReaGBA package with ReaPack.");
    next->data=paths.data;
    if(const auto* test=std::getenv("REAGBA_EXTENSION_TEST_DIR");test && *test) {
        next->testDirectory=fs::u8path(test);next->data=next->testDirectory/"data";
    }
    const auto windowSettings=next->data/"config"/"window.json";
    if(fs::is_regular_file(windowSettings))try {next->windowState.Restore(Json::parse(ReadBytes(windowSettings,65536)));}catch(...){}
    const auto preferencesPath=next->data/"config"/"preferences.json";
    if(fs::is_regular_file(preferencesPath))try {
        const auto preferences=Json::parse(ReadBytes(preferencesPath,1024*1024));
        const auto initial=preferences.value("last_rom_directory",preferences.value("rom_directory",std::string()));
        if(!initial.empty())next->lastRomDirectory=fs::u8path(initial);
    }catch(...){}
    SDL_SetMainReady();
    if(SDL_InitSubSystem(SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)!=0)throw std::runtime_error(SDL_GetError());
    next->sdl=true;
    next->manager=std::make_unique<EmulatorManager>(paths.roms,next->data);
    next->audio=std::make_unique<AudioEngine>(*next->manager);
    next->input=std::make_unique<InputManager>();
    next->bridge=std::make_unique<WebBridge>(*next->manager);
    HostConfig config;
    config.parent=plugin->hwnd_main;config.uiDirectory=ui;config.dataDirectory=next->data/"WebViewData";
    config.manager=next->manager.get();config.input=next->input.get();
#ifdef REAGBA_HELPER_NAME
    config.helperPath=next->root/"extension"/REAGBA_HELPER_NAME;
#endif
    auto* current=next.get();
    config.receive=[current](std::string text) {
        if(text.size()>65536 || current->requests.size()>=128)return false;
        auto request=Json::parse(text,nullptr,false);
        if(!request.is_object() || !request.value("action",Json()).is_string())return false;
        current->requests.push_back(std::move(request));return true;
    };
    config.close=[current] {
        PersistWindowState(true);
        if(!current->testDirectory.empty())WriteJSON(current->testDirectory/"host-closed.json",{{"reason","window close requested"}});
        closing=true;
    };
    config.error=[current](std::string error) {
        if(!current->testDirectory.empty())WriteJSON(current->testDirectory/"host-error.json",{{"error",error}});
        pendingError=std::move(error);closing=true;
    };
    next->host=CreateWebViewHost(std::move(config));
    session=std::move(next);
    const bool initiallyDocked=session->windowState.docked;
    ShowFloating();
    if(initiallyDocked)SetDocked(true);else PersistWindowState(true);
}
Json Viewport(const Json& command) {
    const auto& v=command.at("rect");
    auto number=[&](const char* key) {double x=v.at(key).get<double>();if(!std::isfinite(x)||std::abs(x)>1000000)throw std::runtime_error("Invalid viewport");return x;};
    session->host->SetViewport({number("x"),number("y"),number("width"),number("height"),
        number("clipTop"),number("clipBottom"),number("clientWidth"),v.at("visible").get<bool>()});
    if(!session->testDirectory.empty())session->lastLayout=command.value("layout",Json::object());
    return true;
}
void Request(Json command) {
    const auto action=command.at("action").get<std::string>();
    const auto id=command.value("id",Json());
    Json reply={{"type","reply"},{"id",id},{"action",action},{"ok",true},{"result",true}};
    try {
        if(action=="game_viewport")reply["result"]=Viewport(command);
        else if(action=="keyboard_context")session->host->BlockKeyboard(command.at("blocked").get<bool>());
        else if(action=="focus_game")session->host->FocusGame();
        else if(action=="toggle_dock")SetDocked(!IsDocked());
        else if(action=="fullscreen")throw std::runtime_error("Use REAPER docking or resize the floating window.");
        else {
            if(action=="open_rom") {
                char path[8192]{};
                bool selected=false;
                if(getUserFileName) {
                    auto initial=session->lastRomDirectory.u8string();
                    if(initial.empty() && command.value("initial_path",Json()).is_string())initial=command["initial_path"].get<std::string>();
                    selected=getUserFileName(1,"ReaGBA: Open GBA ROM",initial.c_str(),"GBA ROM|*.gba",path,sizeof(path));
                } else selected=GetUserFileNameForRead(path,"ReaGBA: Open GBA ROM","gba");
                if(!selected) {reply["result"]=nullptr;session->host->Post(reply.dump());return;}
                session->lastRomDirectory=fs::u8path(path).parent_path();
                command={{"action","load_rom"},{"path",path},{"id",id}};
            } else if(action=="select_rom_directory") {
                if(!getUserFileName)throw std::runtime_error("This REAPER version does not provide a folder chooser");
                char path[8192]{};
                auto initial=command.value("initial_path",Json()).is_string()?command["initial_path"].get<std::string>():std::string();
                if(!getUserFileName(3,"ReaGBA: Select ROM Folder",initial.c_str(),"",path,sizeof(path))) {reply["result"]=nullptr;session->host->Post(reply.dump());return;}
                session->lastRomDirectory=fs::u8path(path);
                command={{"action","set_settings"},{"settings",{{"rom_directory",path},{"last_rom_directory",path}}},{"id",id}};
            }
            const auto actual=command.at("action").get<std::string>();
            auto inbox=session->inbox;
            session->bridge->Request(command.dump(),[inbox,id,actual](Json result) {
                result["type"]="reply";result["id"]=id;result["action"]=actual;inbox->Put(std::move(result));
            });
            return;
        }
    } catch(const std::exception& e) {reply["ok"]=false;reply["error"]=e.what();}
    session->inbox->Put(std::move(reply));
}
void Timer() {
    if(inTimer)return;inTimer=true;
    try {
        if(closing)Close();
        if(!pendingError.empty()) {auto error=std::move(pendingError);pendingError.clear();ShowMessageBox(error.c_str(),"ReaGBA",0);}
        if(session) {
            auto& s=*session;
            s.host->Tick();
            for(int count=0;count<32 && !s.requests.empty();++count) {auto request=std::move(s.requests.front());s.requests.pop_front();Request(std::move(request));}
            const auto now=std::chrono::steady_clock::now();
            if(!s.statusPending && now-s.lastState>std::chrono::milliseconds(250)) {
                PersistWindowState();
                s.lastState=now;s.statusPending=true;auto inbox=s.inbox;
                s.manager->Submit({{"action","get_emulator_state"}},[inbox](Json result){result["type"]="state";inbox->Put(std::move(result));});
            }
            std::deque<Json> replies;
            {std::lock_guard<std::mutex> lock(s.inbox->mutex);replies.swap(s.inbox->replies);}
            for(auto& reply:replies) {
                const auto type=reply.value("type",std::string()),action=reply.value("action",std::string());
                if(reply.value("ok",false)) {
                    if(action=="get_settings" || action=="set_settings") {
                        const auto& settings=reply["result"];s.input->Configure(settings);
                        s.host->SetVideo({settings.value("integer_scaling",true),settings.value("filter",std::string("nearest"))=="linear",settings.value("vsync",true),ParseShaderPreset(settings.value("shader",std::string("none")))});
                    }
                    if(type=="state" || action=="get_emulator_state" || action=="load_rom" || action=="load_state" || action=="start" || action=="pause" || action=="stop" || action=="reset" || action=="set_speed") {
                        auto& state=reply["result"];state["reaper"]=true;state["docked"]=IsDocked();
                        if(type=="state") {
                            s.statusPending=false;s.lastStatus=state;
                            if(!s.testDirectory.empty()) {auto sample=state;sample["host"]=s.host->Diagnostics();sample["layout"]=s.lastLayout;s.testStates.push_back(std::move(sample));WriteJSON(s.testDirectory/"status-history.json",s.testStates);}
                        }
                    }
                } else if(type=="state")s.statusPending=false;
                s.host->Post(reply.dump());
                if(reply.value("ok",false) && (action=="load_rom" || action=="start"))s.host->FocusGame();
                if(!s.testDirectory.empty() && type=="reply") {s.testLog.push_back(reply);WriteJSON(s.testDirectory/"responses.json",s.testLog);}
            }
            if(!s.testDirectory.empty()) {
                auto status=s.lastStatus;status["host"]=s.host->Diagnostics();status["audio_device"]=s.audio->Available();status["docked"]=IsDocked();
                WriteJSON(s.testDirectory/"status.json",status);
                const auto request=std::string(GetExtState("ReaGBA","extension_test_request"));
                if(!request.empty() && request!=s.lastTestRequest) {s.lastTestRequest=request;s.requests.push_back(Json::parse(request));}
            }
        }
    } catch(const std::exception& e) {
        pendingError=e.what();closing=true;
        if(session && !session->testDirectory.empty()) {
            try{WriteJSON(session->testDirectory/"host-error.json",{{"error",pendingError}});}catch(...){}
        }
    }
      catch(...) {pendingError="Unexpected native extension error";closing=true;}
    inTimer=false;
}
bool OnCommand(int command,int) {
    if(command!=showCommand)return false;
    try{Open();}catch(const std::exception& e){pendingError=e.what();closing=true;}
    return true;
}
int WindowInfo(HWND window,INT_PTR type) {
    if(!session)return 0;
    const auto root=static_cast<HWND>(session->host->Window());
    if(window!=root && !IsChild(root,window))return 0;
    return type==0||type==1 ? 1 : 0;
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE,reaper_plugin_info_t* rec) {
    if(!rec) {
        if(plugin) {plugin->Register("-timer",reinterpret_cast<void*>(Timer));plugin->Register("-hookcommand",reinterpret_cast<void*>(OnCommand));plugin->Register("-hwnd_info",reinterpret_cast<void*>(WindowInfo));plugin->Register("-gaccel",&accelerator);}
        try{Close(true);}catch(...){}plugin=nullptr;getUserFileName=nullptr;return 0;
    }
    if(rec->caller_version!=REAPER_PLUGIN_VERSION || !rec->GetFunc || REAPERAPI_LoadAPI(rec->GetFunc))return 0;
    plugin=rec;getUserFileName=reinterpret_cast<GetUserFileNameAPI>(rec->GetFunc("GetUserFileName"));showCommand=rec->Register("command_id",const_cast<char*>("REAGBA_SHOW"));if(!showCommand)return 0;
    accelerator.accel.cmd=static_cast<unsigned short>(showCommand);accelerator.desc="zaibuyidao: ReaGBA";
    rec->Register("gaccel",&accelerator);rec->Register("hookcommand",reinterpret_cast<void*>(OnCommand));
    rec->Register("hwnd_info",reinterpret_cast<void*>(WindowInfo));rec->Register("timer",reinterpret_cast<void*>(Timer));
    return 1;
}
