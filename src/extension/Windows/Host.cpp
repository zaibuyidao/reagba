#include "extension/Host.h"
#include <windows.h>
#include <shlwapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <stdexcept>
#include <vector>
#include <commctrl.h>
#include <imm.h>
#include <cmath>
#include "platform/Windows/Renderer.h"
#include "platform/Windows/Keyboard.h"

namespace reagba {
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace {
std::string Utf8(const wchar_t* value) {
    if (!value) return {};
    int size = WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    std::string out(static_cast<size_t>(size),0);
    WideCharToMultiByte(CP_UTF8,0,value,-1,out.data(),size,nullptr,nullptr);
    if (!out.empty()) out.pop_back(); return out;
}
std::wstring Wide(const std::string& value) {
    int size = MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring out(static_cast<size_t>(size),0);
    MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),size); return out;
}
struct State {
    HostConfig config;
    HWND window = nullptr, widget = nullptr, game = nullptr;
    std::unique_ptr<D3DRenderer> renderer;
    GameViewport viewport;
    VideoSettings video;
    bool blocked = false;
    uint64_t keyEvents = 0;
    bool alive = true, ready = false;
    std::wstring url;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    EventRegistrationToken message{},navigation{},newWindow{},permission{};
    bool Active() const {
        if (!window || !IsWindowVisible(window) || IsIconic(GetAncestor(window,GA_ROOT))) return false;
        GUITHREADINFO info{sizeof(info)};
        return GetGUIThreadInfo(0,&info) && info.hwndFocus && (info.hwndFocus==window || IsChild(window,info.hwndFocus));
    }
    void Focus() { blocked=false;config.input->Clear();if(game)SetFocus(game); }
    bool Key(UINT vk,bool down) {
        if (blocked || !Active()) return false;
        bool matched=false;
        for(auto scan:config.input->Mapping()) {
            const auto mapped=VirtualKey(scan);
            if(mapped==int(vk)||(vk==VK_SHIFT&&(mapped==VK_LSHIFT||mapped==VK_RSHIFT))) {
                config.input->Key(scan,down);matched=true;++keyEvents;
            }
        }
        return matched;
    }
    void Render() {
        if(!alive||!renderer) return;
        const bool active=Active()&&!blocked;
        if(!active)config.input->Clear();
        else for(auto scan:config.input->Mapping()) { const int vk=VirtualKey(scan);config.input->Key(scan,vk&&(GetAsyncKeyState(vk)&0x8000)); }
        config.manager->SetInput(config.input->Poll(active));
        config.manager->SetFastForward(config.input->FastForward());
        if(config.manager->frames.Consume())renderer->Upload(config.manager->frames.Front());
        if(IsWindowVisible(game))renderer->Draw(video);
    }
    void Resize() {
        if (!window) return;
        RECT rect{}; GetClientRect(window,&rect);
        MoveWindow(widget,0,0,rect.right,rect.bottom,TRUE);
        if(controller) {controller->put_Bounds(rect);controller->NotifyParentWindowPositionChanged();}
        const auto& v=viewport;
        const double scale=rect.right/std::max(1.0,v.clientWidth);
        const int x=int(std::lround(v.x*scale)),y=int(std::lround(v.y*scale));
        const int width=int(std::lround(v.width*scale)),height=int(std::lround(v.height*scale));
        const int top=std::max(0,int(std::ceil(v.clipTop*scale))-y),bottom=std::min(height,int(std::floor(v.clipBottom*scale))-y);
        if(!v.visible||width<=0||height<=0||bottom<=top) {ShowWindow(game,SW_HIDE);SetWindowRgn(widget,nullptr,TRUE);return;}
        SetWindowRgn(game,CreateRectRgn(0,top,width,bottom),FALSE);
        auto region=CreateRectRgn(0,0,rect.right,rect.bottom),hole=CreateRectRgn(x,y+top,x+width,y+bottom);
        CombineRgn(region,region,hole,RGN_DIFF);DeleteObject(hole);SetWindowRgn(widget,region,TRUE);
        SetWindowPos(game,HWND_TOP,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        renderer->Resize(width,height);
    }
    void Fail(const char* text) { if (alive && config.error) config.error(text); }
};
LRESULT CALLBACK GameProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto* state=reinterpret_cast<State*>(data);
    if(message==WM_LBUTTONDOWN){state->Focus();return 0;}
    if(message==WM_GETDLGCODE)return DLGC_WANTALLKEYS|DLGC_WANTARROWS|DLGC_WANTCHARS;
    if(message==WM_KEYDOWN||message==WM_KEYUP){state->Key(UINT(wp),message==WM_KEYDOWN);return 0;}
    if(message==WM_KILLFOCUS){state->config.input->Clear();state->config.manager->SetInput(0);state->config.manager->SetFastForward(false);}
    return DefSubclassProc(hwnd,message,wp,lp);
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        state = static_cast<State*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
        state->window = hwnd;
    }
    if (state) {
        if(message==WM_TIMER && wp==73) {try{state->Render();}catch(const std::exception& e){KillTimer(hwnd,73);state->Fail(e.what());}return 0;}
        if (message == WM_SIZE || message == WM_MOVE) { state->Resize(); return 0; }
        if (message == WM_SETFOCUS && state->controller) {
            if(state->viewport.visible)state->Focus();else state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC); return 0;
        }
        if (message == WM_CLOSE) { if (state->config.close) state->config.close(); return 0; }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_NCDESTROY) { state->window = nullptr; SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); }
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
class WindowsHost final : public WebViewHost {
    std::shared_ptr<State> state_ = std::make_shared<State>();
    bool comOwned_ = false;
public:
    explicit WindowsHost(HostConfig config) {
        state_->config = std::move(config);
        const HRESULT com = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if (FAILED(com)) throw std::runtime_error("WebView2 requires the REAPER UI thread to use COM STA");
        comOwned_ = true;
        try { Initialize(); }
        catch (...) { Shutdown(); throw; }
    }
    void Initialize() {
        WNDCLASSW klass{};
        klass.lpfnWndProc = WindowProc;
        // The DLL owns its class; unregistering on shutdown avoids a dangling wndproc.
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&WindowProc),&klass.hInstance);
        klass.lpszClassName = L"ReaGBADockContent";
        klass.hCursor = LoadCursor(nullptr,IDC_ARROW);
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Could not register ReaGBA dock window");
        HWND hwnd = CreateWindowExW(0,klass.lpszClassName,L"ReaGBA",WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,CW_USEDEFAULT,760,900,static_cast<HWND>(state_->config.parent),nullptr,klass.hInstance,state_.get());
        if (!hwnd) throw std::runtime_error("Could not create ReaGBA dock container");
        state_->widget=CreateWindowExW(0,L"STATIC",L"ReaGBA WebView",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,0,0,1,1,hwnd,nullptr,klass.hInstance,nullptr);
        state_->game=CreateWindowExW(0,L"STATIC",L"GBA Native View",WS_CHILD|WS_CLIPSIBLINGS|SS_NOTIFY,0,0,240,160,hwnd,nullptr,klass.hInstance,nullptr);
        if(!state_->widget||!state_->game)throw std::runtime_error("Could not create native GBA view");
        ImmAssociateContext(state_->game,nullptr);
        SetWindowSubclass(state_->game,GameProc,74,reinterpret_cast<DWORD_PTR>(state_.get()));
        state_->renderer=std::make_unique<D3DRenderer>(state_->game);
        SetTimer(hwnd,73,16,nullptr);
        std::filesystem::create_directories(state_->config.dataDirectory);
        const auto path = (state_->config.uiDirectory / "index.html").wstring();
        std::vector<wchar_t> url(32768); DWORD urlSize = static_cast<DWORD>(url.size());
        if (FAILED(UrlCreateFromPathW(path.c_str(),url.data(),&urlSize,0))) throw std::runtime_error("Invalid UI path");
        state_->url = url.data();
        std::weak_ptr<State> weak = state_;
        const auto hr = CreateCoreWebView2EnvironmentWithOptions(nullptr,state_->config.dataDirectory.c_str(),nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak](HRESULT error, ICoreWebView2Environment* environment)->HRESULT {
                auto s = weak.lock(); if (!s || !s->alive) return S_OK;
                if (FAILED(error) || !environment) { s->Fail("Cannot initialize WebView2. Install Microsoft WebView2 Evergreen Runtime."); return S_OK; }
                auto controllerResult = environment->CreateCoreWebView2Controller(s->widget,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [weak](HRESULT error, ICoreWebView2Controller* controller)->HRESULT {
                        auto s = weak.lock(); if (!s || !s->alive) { if (controller) controller->Close(); return S_OK; }
                        if (FAILED(error) || !controller) { s->Fail("Cannot create WebView2 controller"); return S_OK; }
                        s->controller = controller; controller->get_CoreWebView2(&s->view);
                        EventRegistrationToken keyToken{};
                        controller->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                            [weak](ICoreWebView2Controller*,ICoreWebView2AcceleratorKeyPressedEventArgs* args)->HRESULT {
                                auto p=weak.lock();if(!p||!p->alive)return S_OK;
                                UINT vk=0;COREWEBVIEW2_KEY_EVENT_KIND kind{};args->get_VirtualKey(&vk);args->get_KeyEventKind(&kind);
                                if(vk==VK_PROCESSKEY){COREWEBVIEW2_PHYSICAL_KEY_STATUS physical{};args->get_PhysicalKeyStatus(&physical);vk=MapVirtualKeyW(physical.ScanCode,MAPVK_VSC_TO_VK_EX);}
                                if(kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN||kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_UP)
                                    if(p->Key(vk,kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN))args->put_Handled(TRUE);
                                return S_OK;
                            }).Get(),&keyToken);
                        if (!s->view) { s->Fail("Cannot access WebView2"); return S_OK; }
                        ComPtr<ICoreWebView2Settings> settings; s->view->get_Settings(&settings);
                        if (settings) {
                            settings->put_AreDefaultContextMenusEnabled(FALSE);
                            settings->put_AreDevToolsEnabled(FALSE);
                            settings->put_AreHostObjectsAllowed(FALSE);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_IsWebMessageEnabled(TRUE);
                        }
                        s->view->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                            [weak](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT {
                                auto p = weak.lock(); if (!p || !p->alive) return S_OK;
                                LPWSTR source = nullptr; args->get_Source(&source);
                                bool trusted = source && p->url == source; CoTaskMemFree(source);
                                if (!trusted) return S_OK;
                                LPWSTR text = nullptr;
                                if (SUCCEEDED(args->get_WebMessageAsJson(&text))) {
                                    p->ready = true;
                                    const bool accepted = p->config.receive(Utf8(text)); CoTaskMemFree(text);
                                    if (!accepted) p->view->PostWebMessageAsJson(L"{\"type\":\"error\",\"error\":\"Native queue busy; retry the operation\"}");
                                }
                                return S_OK;
                            }).Get(),&s->message);
                        s->view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                            [weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT {
                                auto p = weak.lock(); LPWSTR uri = nullptr; args->get_Uri(&uri);
                                args->put_Cancel(!p || !uri || p->url != uri); CoTaskMemFree(uri); return S_OK;
                            }).Get(),&s->navigation);
                        s->view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                            [](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* a)->HRESULT { a->put_Handled(TRUE); return S_OK; }).Get(),&s->newWindow);
                        s->view->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
                            [](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* a)->HRESULT { a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK; }).Get(),&s->permission);
                        s->Resize(); controller->put_IsVisible(TRUE);
                        if (FAILED(s->view->Navigate(s->url.c_str()))) s->Fail("Cannot load ReaGBA UI");
                        return S_OK;
                    }).Get());
                if (FAILED(controllerResult)) s->Fail("Cannot start WebView2 controller");
                return S_OK;
            }).Get());
        if (FAILED(hr)) state_->Fail("WebView2 Runtime is missing. Install Microsoft WebView2 Evergreen Runtime and reopen ReaGBA.");
    }
    void Shutdown() {
        state_->alive = false; state_->config.receive = {}; state_->config.close = {}; state_->config.error = {};
        if (state_->view) {
            state_->view->remove_WebMessageReceived(state_->message);
            state_->view->remove_NavigationStarting(state_->navigation);
            state_->view->remove_NewWindowRequested(state_->newWindow);
            state_->view->remove_PermissionRequested(state_->permission);
            state_->view->Stop();
        }
        if (state_->controller) state_->controller->Close();
        state_->view.Reset(); state_->controller.Reset();
        if(state_->window)KillTimer(state_->window,73);
        if(state_->game)RemoveWindowSubclass(state_->game,GameProc,74);
        state_->renderer.reset();
        if (state_->window) DestroyWindow(state_->window);
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&WindowProc),&module);
        UnregisterClassW(L"ReaGBADockContent",module);
        if (comOwned_) { CoUninitialize(); comOwned_ = false; }
    }
    ~WindowsHost() override { Shutdown(); }
    void* Window() const override { return state_->window; }
    void SetTitle(const std::string& title) override { SetWindowTextW(state_->window,Wide(title).c_str()); }
    void Post(const std::string& json) override {
        // The UI requests a full state after registering its receiver.
        if (state_->ready && state_->view) state_->view->PostWebMessageAsJson(Wide(json).c_str());
    }
    void SetViewport(GameViewport viewport) override {state_->viewport=viewport;state_->Resize();}
    void SetVideo(VideoSettings video) override {state_->video=video;}
    void FocusGame() override {state_->Focus();}
    void BlockKeyboard(bool blocked) override {state_->blocked=blocked;state_->config.input->Clear();}
    bool Active() const override {return state_->Active();}
    bool KeyboardBlocked() const override {return state_->blocked;}
    Json Diagnostics() const override {
        RECT client{},game{};GetClientRect(state_->window,&client);GetWindowRect(state_->game,&game);
        MapWindowPoints(HWND_DESKTOP,state_->window,reinterpret_cast<POINT*>(&game),2);
        return {{"active",Active()},{"keyboard_blocked",state_->blocked},{"key_events",state_->keyEvents},
            {"client_width",client.right},{"client_height",client.bottom},
            {"viewport",{{"x",game.left},{"y",game.top},{"width",game.right-game.left},{"height",game.bottom-game.top},{"visible",bool(IsWindowVisible(state_->game))}}}};
    }
    void Tick() override {
        if (state_->controller) state_->controller->put_IsVisible(IsWindowVisible(state_->window));
    }
};
}
std::unique_ptr<WebViewHost> CreateWebViewHost(HostConfig config) { return std::make_unique<WindowsHost>(std::move(config)); }
}
