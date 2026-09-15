#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include "extension/Host.h"
#include "extension/ScriptDelivery.h"
#include "reaper/ReaperAPI.h"
#include "swell/swell-dlggen.h"
#include <stdexcept>
#include "video/GLRenderer.h"
#include "scancodes_darwin.h"

// Match the Linux host's empty, pixel-sized WebView container.
static SWELL_DialogRegHelper reaGBADialog(&SWELL_curmodule_dialogresource_head,
    [](HWND, int) {}, 101, SWELL_DLG_WS_RESIZABLE, "ReaGBA", 760, 900, 1.0, 1.0);

@interface ReaGBAGameView : NSOpenGLView {
@public
    reagba::InputManager* input;
    bool* blocked;
}
@end
@implementation ReaGBAGameView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)mouseDown:(NSEvent*)event { (void)event;*blocked=false;input->Clear();[self.window makeFirstResponder:self]; }
- (void)keyDown:(NSEvent*)event { if(!*blocked && event.keyCode<sizeof(darwin_scancode_table)/sizeof(darwin_scancode_table[0]))input->Key(darwin_scancode_table[event.keyCode],true); }
- (void)keyUp:(NSEvent*)event { if(event.keyCode<sizeof(darwin_scancode_table)/sizeof(darwin_scancode_table[0]))input->Key(darwin_scancode_table[event.keyCode],false); }
- (void)flagsChanged:(NSEvent*)event {
    if(event.keyCode<sizeof(darwin_scancode_table)/sizeof(darwin_scancode_table[0]))input->Key(darwin_scancode_table[event.keyCode],(event.modifierFlags&NSEventModifierFlagShift)!=0);
}
@end

@interface ReaGBADelegate : NSObject <WKScriptMessageHandler, WKNavigationDelegate, WKUIDelegate> {
@public
    reagba::HostConfig config;
    BOOL alive;
    BOOL ready;
}
@property(nonatomic, strong) NSURL* entryURL;
@end
@implementation ReaGBADelegate
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)controller;
    if (!alive || !message.frameInfo.isMainFrame || ![message.frameInfo.request.URL isEqual:self.entryURL]) return;
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:message.body options:0 error:&error];
    if (!data || error || data.length > 16384) return;
    ready = YES;
    if (config.receive && !config.receive(std::string(static_cast<const char*>(data.bytes),data.length))) {
        const std::string script = reagba::DeliveryScript(R"({"type":"error","error":"Native queue busy; retry the operation"})");
        [message.webView evaluateJavaScript:[NSString stringWithUTF8String:script.c_str()] completionHandler:nil];
    }
}
- (void)webView:(WKWebView*)webView decidePolicyForNavigationAction:(WKNavigationAction*)action decisionHandler:(void (^)(WKNavigationActionPolicy))handler {
    (void)webView;
    handler(alive && action.targetFrame.isMainFrame && [action.request.URL isEqual:self.entryURL]
        ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}
- (WKWebView*)webView:(WKWebView*)webView createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
 forNavigationAction:(WKNavigationAction*)action windowFeatures:(WKWindowFeatures*)features {
    (void)webView; (void)configuration; (void)action; (void)features; return nil;
}
- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
    (void)webView; (void)navigation;
    if (alive && error.code != NSURLErrorCancelled && config.error) config.error(error.localizedDescription.UTF8String);
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
    (void)webView;
    if (alive && config.error) config.error("WebKit content process exited. Reopen ReaGBA.");
}
- (void)webView:(WKWebView*)webView requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin
 initiatedByFrame:(WKFrameInfo*)frame type:(WKMediaCaptureType)type decisionHandler:(void (^)(WKPermissionDecision))handler API_AVAILABLE(macos(12.0)) {
    (void)webView; (void)origin; (void)frame; (void)type; handler(WKPermissionDecisionDeny);
}
@end

namespace reagba {
namespace {
class MacHost final : public WebViewHost {
    HWND window_ = nullptr;
    WKWebView* view_ = nil;
    ReaGBADelegate* delegate_ = nil;
    ReaGBAGameView* game_ = nil;
    NSView* content_ = nil;
    NSTimer* timer_ = nil;
    id keyboardMonitor_ = nil;
    bool blocked_ = false;
    GameViewport viewport_;
    VideoSettings video_;
    std::unique_ptr<GLRenderer> renderer_;
    int width_ = 0,height_ = 0,offset_ = 0;
    void Render() {
        auto& config=delegate_->config;
        const bool active=Active()&&!blocked_;
        config.manager->SetInput(config.input->Poll(active));config.manager->SetFastForward(config.input->FastForward());
        if(!renderer_)return;
        [game_.openGLContext makeCurrentContext];
        if(config.manager->frames.Consume())renderer_->Upload(config.manager->frames.Front());
        if(!game_.hidden && width_>0 && height_>0) {renderer_->Draw(width_,height_,video_,offset_);[game_.openGLContext flushBuffer];}
    }
    void Layout() {
        const auto& v=viewport_;const CGFloat scale=content_.bounds.size.width/std::max(1.0,v.clientWidth);
        const CGFloat width=v.width*scale,height=v.height*scale,x=v.x*scale,y=v.y*scale;
        const CGFloat top=std::max(0.0,v.clipTop*scale-y),bottom=std::min(double(height),v.clipBottom*scale-y);
        game_.hidden=!v.visible||width<=0||height<=0||bottom<=top;
        if(game_.hidden)return;
        game_.frame=NSMakeRect(x,content_.bounds.size.height-y-bottom,width,bottom-top);
        [game_.openGLContext update];
        const CGFloat pixels=[game_ convertSizeToBacking:NSMakeSize(1,1)].width;
        width_=int(std::lround(width*pixels));height_=int(std::lround(height*pixels));offset_=int(std::lround((bottom-height)*pixels));
    }
    static INT_PTR DialogProc(HWND window, UINT message, WPARAM, LPARAM lp) {
        // SWELL's WindowLong API uses LONG_PTR, including on 64-bit platforms.
        if (message == WM_INITDIALOG) { SetWindowLong(window,GWL_USERDATA,lp); return TRUE; }
        auto* self = reinterpret_cast<MacHost*>(GetWindowLong(window,GWL_USERDATA));
        if (self && message == WM_CLOSE) { if (self->delegate_->config.close) self->delegate_->config.close(); return TRUE; }
        return FALSE;
    }
public:
    explicit MacHost(HostConfig config) {
        try { Initialize(std::move(config)); } catch (...) { Shutdown(); throw; }
    }
    void Initialize(HostConfig config) {
        delegate_ = [ReaGBADelegate new]; delegate_->config = std::move(config); delegate_->alive = YES;
        window_ = CreateDialogParam(nullptr,MAKEINTRESOURCE(101),static_cast<HWND>(delegate_->config.parent),DialogProc,reinterpret_cast<LPARAM>(this));
        if (!window_) throw std::runtime_error("Cannot create SWELL dock container");
        NSView* content = (__bridge NSView*)GetDlgItem(window_,0);content_=content;
        WKWebViewConfiguration* webConfig = [WKWebViewConfiguration new];
        [webConfig.userContentController addScriptMessageHandler:delegate_ name:@"reagba"];
        webConfig.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        view_ = [[WKWebView alloc] initWithFrame:content.bounds configuration:webConfig];
        view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        view_.navigationDelegate = delegate_; view_.UIDelegate = delegate_;
        [content addSubview:view_];
        NSOpenGLPixelFormatAttribute attributes[]={NSOpenGLPFAOpenGLProfile,NSOpenGLProfileVersion3_2Core,NSOpenGLPFADoubleBuffer,NSOpenGLPFAColorSize,24,0};
        auto* format=[[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
        game_=[[ReaGBAGameView alloc] initWithFrame:NSMakeRect(0,0,240,160) pixelFormat:format];
        game_->input=delegate_->config.input;game_->blocked=&blocked_;
        game_.wantsBestResolutionOpenGLSurface=YES;game_.hidden=YES;
        [content addSubview:game_ positioned:NSWindowAbove relativeTo:view_];
        [game_.openGLContext makeCurrentContext];renderer_=std::make_unique<GLRenderer>();
        keyboardMonitor_=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown|NSEventMaskKeyUp handler:^NSEvent*(NSEvent* event){
            if(!Active()||blocked_||event.keyCode>=sizeof(darwin_scancode_table)/sizeof(darwin_scancode_table[0]))return event;
            const auto scan=darwin_scancode_table[event.keyCode];
            for(auto mapped:delegate_->config.input->Mapping())if(scan==mapped){delegate_->config.input->Key(scan,event.type==NSEventTypeKeyDown);return nil;}
            return event;
        }];
        timer_=[NSTimer scheduledTimerWithTimeInterval:.016 repeats:YES block:^(NSTimer*){try{Layout();Render();}catch(const std::exception& e){if(delegate_->config.error)delegate_->config.error(e.what());}}];
        NSURL* directory = [NSURL fileURLWithPath:[NSString stringWithUTF8String:delegate_->config.uiDirectory.c_str()] isDirectory:YES];
        delegate_.entryURL = [directory URLByAppendingPathComponent:@"index.html"];
        [view_ loadFileURL:delegate_.entryURL allowingReadAccessToURL:directory];
    }
    void Shutdown() {
        [timer_ invalidate];timer_=nil;
        if(keyboardMonitor_){[NSEvent removeMonitor:keyboardMonitor_];keyboardMonitor_=nil;}
        [game_.openGLContext makeCurrentContext];renderer_.reset();[game_ removeFromSuperview];game_=nil;
        if(delegate_) {delegate_->alive = NO; delegate_->config.receive = {}; delegate_->config.close = {}; delegate_->config.error = {};}
        [view_.configuration.userContentController removeScriptMessageHandlerForName:@"reagba"];
        view_.navigationDelegate = nil; view_.UIDelegate = nil;
        [view_ stopLoading]; [view_ removeFromSuperview]; view_ = nil;
        if (window_) { SetWindowLong(window_,GWL_USERDATA,0); DestroyWindow(window_); window_=nullptr; }
        delegate_ = nil;content_=nil;
    }
    ~MacHost() override { Shutdown(); }
    void* Window() const override { return window_; }
    void SetTitle(const std::string& title) override { SetWindowText(window_,title.c_str()); }
    void Post(const std::string& json) override {
        if (!delegate_->ready) return;
        auto script = DeliveryScript(json);
        [view_ evaluateJavaScript:[NSString stringWithUTF8String:script.c_str()] completionHandler:nil];
    }
    void SetViewport(GameViewport viewport) override {viewport_=viewport;Layout();}
    void SetVideo(VideoSettings video) override {video_=video;GLint sync=video.vsync?1:0;[game_.openGLContext setValues:&sync forParameter:NSOpenGLCPSwapInterval];}
    void FocusGame() override {blocked_=false;delegate_->config.input->Clear();[content_.window makeFirstResponder:game_];}
    void BlockKeyboard(bool blocked) override {blocked_=blocked;delegate_->config.input->Clear();}
    bool Active() const override {
        if(!content_.window.isKeyWindow || content_.hiddenOrHasHiddenAncestor)return false;
        auto* responder=content_.window.firstResponder;
        return [responder isKindOfClass:[NSView class]] && [(NSView*)responder isDescendantOf:content_];
    }
    bool KeyboardBlocked() const override {return blocked_;}
    Json Diagnostics() const override {return {{"active",Active()},{"keyboard_blocked",blocked_},{"client_width",content_.bounds.size.width},{"client_height",content_.bounds.size.height}};}
    void Tick() override {Layout();}
};
}
std::unique_ptr<WebViewHost> CreateWebViewHost(HostConfig config) { return std::make_unique<MacHost>(std::move(config)); }
}
