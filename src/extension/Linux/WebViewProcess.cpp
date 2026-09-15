// Private UI helper. Isolates GTK3/WebKitGTK from REAPER's GDK version.
#include "extension/ScriptDelivery.h"
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gdk/gdkx.h>
#include <glib-unix.h>
#include <webkit2/webkit2.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <deque>
#include <string>
#include <memory>
#include <set>
#include <algorithm>
#include <cstring>
#include "extension/Linux/SharedFrame.h"
#include "video/GLRenderer.h"

namespace {
using Json = nlohmann::json;
struct App {
    GtkWidget* plug = nullptr;
    WebKitWebView* view = nullptr;
    std::string uri,input,output;
    guint writeWatch = 0;
    bool javascriptReady = false, evaluating = false;
    std::deque<std::string> delivery;
    Window parent = 0;
    GtkWidget* overlay = nullptr;
    GtkWidget* game = nullptr;
    reagba::SharedFrame* shared = nullptr;
    reagba::Frame pixels{};
    uint64_t sequence = 0;
    std::unique_ptr<reagba::GLRenderer> renderer;
    reagba::GameViewport viewport;
    reagba::VideoSettings video;
    int originalWidth = 0,originalHeight = 0,offset = 0;
    bool blocked = false,active = false;
    std::set<std::string> keys{"J","K","Space","Return","D","A","W","S","E","Q","R"};
};
void Send(App* app, const std::string& text);
gboolean Write(gint fd, GIOCondition condition, gpointer data) {
    auto* app = static_cast<App*>(data);
    if (condition & (G_IO_HUP | G_IO_ERR)) { app->writeWatch = 0; gtk_main_quit(); return G_SOURCE_REMOVE; }
    auto n = send(fd,app->output.data(),app->output.size(),MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n > 0) app->output.erase(0,static_cast<size_t>(n));
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { app->writeWatch = 0; gtk_main_quit(); return G_SOURCE_REMOVE; }
    if (app->output.empty()) { app->writeWatch = 0; return G_SOURCE_REMOVE; }
    return G_SOURCE_CONTINUE;
}
void Send(App* app, const std::string& text) {
    if (app->output.size()+text.size()>1024*1024) { gtk_main_quit(); return; }
    app->output += text; app->output += '\n';
    if (!app->writeWatch) app->writeWatch = g_unix_fd_add(3,static_cast<GIOCondition>(G_IO_OUT | G_IO_HUP | G_IO_ERR),Write,app);
}
void Deliver(App* app);
void Evaluated(GObject* source,GAsyncResult* result,gpointer data) {
    auto* app = static_cast<App*>(data); GError* error = nullptr;
    JSCValue* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(source),result,&error);
    if (value) g_object_unref(value);
    if (error) { g_error_free(error); }
    app->evaluating = false; Deliver(app);
}
void Deliver(App* app) {
    if (!app->javascriptReady || app->evaluating || app->delivery.empty()) return;
    auto script = reagba::DeliveryScript(app->delivery.front()); app->delivery.pop_front(); app->evaluating = true;
    webkit_web_view_evaluate_javascript(app->view,script.c_str(),static_cast<gssize>(script.size()),nullptr,nullptr,nullptr,Evaluated,app);
}
void Layout(App* app) {
    const auto& v=app->viewport;
    const double scale=gtk_widget_get_allocated_width(app->overlay)/std::max(1.0,v.clientWidth);
    const int x=int(std::lround(v.x*scale)),y=int(std::lround(v.y*scale));
    const int width=int(std::lround(v.width*scale)),height=int(std::lround(v.height*scale));
    const int top=std::max(0,int(std::ceil(v.clipTop*scale))-y),bottom=std::min(height,int(std::floor(v.clipBottom*scale))-y);
    if(!v.visible||width<=0||height<=0||bottom<=top){gtk_widget_hide(app->game);return;}
    gtk_widget_set_margin_start(app->game,std::max(0,x));gtk_widget_set_margin_top(app->game,std::max(0,y+top));
    gtk_widget_set_size_request(app->game,width,bottom-top);
    const int pixels=gtk_widget_get_scale_factor(app->game);
    app->originalWidth=width*pixels;app->originalHeight=height*pixels;app->offset=(bottom-height)*pixels;
    gtk_widget_show(app->game);
}
bool Focused(App* app) {
    if(!gtk_widget_get_mapped(app->plug))return false;
    auto* display=gdk_x11_display_get_xdisplay(gtk_widget_get_display(app->plug));
    Window focus=None;int revert=0;XGetInputFocus(display,&focus,&revert);
    const Window own=gdk_x11_window_get_xid(gtk_widget_get_window(app->plug));
    for(int depth=0;depth<32 && focus!=None && focus!=PointerRoot;++depth) {
        if(focus==own)return true;
        Window root,parent,*children=nullptr;unsigned count=0;
        if(!XQueryTree(display,focus,&root,&parent,&children,&count))return false;
        if(children)XFree(children);if(focus==parent)return false;focus=parent;
    }
    return false;
}
std::string KeyName(guint key) {
    switch(key){case GDK_KEY_Return:case GDK_KEY_KP_Enter:return "Return";case GDK_KEY_space:return "Space";
      case GDK_KEY_BackSpace:return "Backspace";case GDK_KEY_Up:return "Up";case GDK_KEY_Down:return "Down";
      case GDK_KEY_Left:return "Left";case GDK_KEY_Right:return "Right";case GDK_KEY_Shift_L:return "Left Shift";case GDK_KEY_Shift_R:return "Right Shift";}
    auto c=gdk_keyval_to_unicode(gdk_keyval_to_upper(key));if(c>0 && c<128)return std::string(1,char(c));return {};
}
gboolean Key(GtkWidget*,GdkEventKey* event,gpointer data) {
    auto* app=static_cast<App*>(data);const auto key=KeyName(event->keyval);
    if(app->blocked || !app->keys.count(key) || !Focused(app))return FALSE;
    Send(app,"@INPUT "+Json{{"active",true},{"key",key},{"down",event->type==GDK_KEY_PRESS}}.dump());return TRUE;
}
gboolean Render(GtkGLArea*,GdkGLContext*,gpointer data) {
    auto* app=static_cast<App*>(data);
    try {
        if(!app->renderer)app->renderer=std::make_unique<reagba::GLRenderer>();
        if(reagba::LockFrame(app->shared)) {
            if(app->sequence!=app->shared->sequence) {app->sequence=app->shared->sequence;app->pixels=app->shared->pixels;pthread_mutex_unlock(&app->shared->mutex);app->renderer->Upload(app->pixels);}
            else pthread_mutex_unlock(&app->shared->mutex);
        }
        app->renderer->Draw(app->originalWidth,app->originalHeight,app->video,app->offset);
    }catch(const std::exception& e){Send(app,std::string("@ERROR ")+e.what());gtk_main_quit();}
    return TRUE;
}
void Handle(App* app,const std::string& line) {
    auto message = Json::parse(line);
    if (message.contains("_host")) {
        const auto action = message.at("_host").get<std::string>();
        auto* display = gdk_x11_display_get_xdisplay(gtk_widget_get_display(app->plug));
        const auto child = gdk_x11_window_get_xid(gtk_widget_get_window(app->plug));
        if (action == "geometry") {
            const auto parent = message.at("parent").get<unsigned long>();
            int x = message.at("x"),y = message.at("y"),width = message.at("width"),height = message.at("height");
            if (parent != app->parent) { XReparentWindow(display,child,parent,x,y); app->parent = parent; }
            gtk_window_resize(GTK_WINDOW(app->plug),width,height);
            XMoveResizeWindow(display,child,x,y,static_cast<unsigned>(width),static_cast<unsigned>(height));
            if (message.at("visible").get<bool>()) {
                gtk_widget_show(app->plug);
                XMoveResizeWindow(display,child,x,y,static_cast<unsigned>(width),static_cast<unsigned>(height));
                XMapWindow(display,child);
            } else gtk_widget_hide(app->plug);
            XFlush(display);
            Layout(app);
        } else if(action=="viewport") {
            auto& v=app->viewport;v={message.at("x"),message.at("y"),message.at("width"),message.at("height"),message.at("clipTop"),message.at("clipBottom"),message.at("clientWidth"),message.at("visible")};Layout(app);
        } else if(action=="keyboard")app->blocked=message.at("blocked");
        else if(action=="settings") {app->video.integerScaling=message.at("integer");app->video.linear=message.at("linear");app->keys.clear();for(const auto& key:message.at("keys"))app->keys.insert(key.get<std::string>());}
        else if(action=="focus_game") {app->blocked=false;XSetInputFocus(display,child,RevertToParent,CurrentTime);gtk_widget_grab_focus(app->game);XFlush(display);}
        else if (action == "focus" && app->parent) {
            XSetInputFocus(display,child,RevertToParent,CurrentTime); XFlush(display);
        }
    } else {
        if (app->delivery.size() >= 256) { Send(app,"@ERROR WebKitGTK JavaScript queue overflow"); gtk_main_quit(); return; }
        app->delivery.push_back(line); Deliver(app);
    }
}
gboolean Read(gint fd,GIOCondition condition,gpointer data) {
    auto* app = static_cast<App*>(data);
    if (condition & (G_IO_HUP | G_IO_ERR)) { gtk_main_quit(); return G_SOURCE_REMOVE; }
    char buffer[65536];
    const auto count = recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);
    if (count == 0) { gtk_main_quit(); return G_SOURCE_REMOVE; }
    if (count < 0) return G_SOURCE_CONTINUE;
    app->input.append(buffer,static_cast<size_t>(count));
    size_t newline;
    try {
        while ((newline = app->input.find('\n')) != std::string::npos) {
            auto line = app->input.substr(0,newline); app->input.erase(0,newline+1); Handle(app,line);
        }
        if (app->input.size() > 16*1024*1024) throw std::runtime_error("State exceeds IPC limit");
    } catch (const std::exception& e) { Send(app,std::string("@ERROR ")+e.what()); gtk_main_quit(); return G_SOURCE_REMOVE; }
    return G_SOURCE_CONTINUE;
}
void Message(WebKitUserContentManager*,WebKitJavascriptResult* result,gpointer data) {
    auto* app = static_cast<App*>(data);
    const char* uri = webkit_web_view_get_uri(app->view);
    if (!uri || app->uri != uri) return;
    auto* value = webkit_javascript_result_get_js_value(result);
    gchar* json = jsc_value_to_json(value,0);
    if (json) {
        if (strlen(json) <= 65536) {
            auto request=Json::parse(json,nullptr,false);
            if(request.is_object() && request.value("action",std::string())=="keyboard_context") {
                app->blocked=request.value("blocked",true);Send(app,"@INPUT "+Json{{"active",Focused(app)}}.dump());
            }
            app->javascriptReady = true; Send(app,json); Deliver(app);
        }
        g_free(json);
    }
}
gboolean Navigation(WebKitWebView*,WebKitPolicyDecision* decision,WebKitPolicyDecisionType type,gpointer data) {
    auto* app = static_cast<App*>(data);
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) { webkit_policy_decision_ignore(decision); return TRUE; }
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        auto* action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        const char* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
        if (!uri || app->uri != uri) { webkit_policy_decision_ignore(decision); return TRUE; }
    }
    return FALSE;
}
}
int main(int argc,char** argv) {
    if (argc != 2) return 2;
    App app;
    auto* memory=mmap(nullptr,sizeof(reagba::SharedFrame),PROT_READ|PROT_WRITE,MAP_SHARED,4,0);
    if(memory==MAP_FAILED)return 5;
    app.shared=static_cast<reagba::SharedFrame*>(memory);close(4);
    g_setenv("GDK_BACKEND","x11",TRUE);
    if (!gtk_init_check(&argc,&argv)) return 3;
    fcntl(3,F_SETFL,fcntl(3,F_GETFL) | O_NONBLOCK);
    GError* error = nullptr;
    char* uri = g_filename_to_uri(argv[1],nullptr,&error);
    if (!uri) { if (error) g_error_free(error); return 4; }
    app.uri = uri; g_free(uri);
    app.plug = gtk_plug_new(0);
    gtk_window_set_default_size(GTK_WINDOW(app.plug),760,900);
    gtk_window_set_decorated(GTK_WINDOW(app.plug),FALSE);
    auto* manager = webkit_user_content_manager_new();
    g_signal_connect(manager,"script-message-received::reagba",G_CALLBACK(Message),&app);
    webkit_user_content_manager_register_script_message_handler(manager,"reagba");
    // An ephemeral context keeps browser data separate from all other applications.
    auto* context = webkit_web_context_new_ephemeral();
    app.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,"web-context",context,"user-content-manager",manager,nullptr));
    g_object_unref(context); g_object_unref(manager);
    auto* settings = webkit_web_view_get_settings(app.view);
    webkit_settings_set_enable_developer_extras(settings,FALSE);
    webkit_settings_set_enable_html5_database(settings,FALSE);
    webkit_settings_set_enable_html5_local_storage(settings,FALSE);
    webkit_settings_set_enable_page_cache(settings,FALSE);
    g_signal_connect(app.view,"decide-policy",G_CALLBACK(Navigation),&app);
    g_signal_connect(app.view,"permission-request",G_CALLBACK(+[](WebKitWebView*,WebKitPermissionRequest* request,gpointer)->gboolean { webkit_permission_request_deny(request); return TRUE; }),nullptr);
    g_signal_connect(app.view,"context-menu",G_CALLBACK(+[](WebKitWebView*,WebKitContextMenu*,GdkEvent*,WebKitHitTestResult*,gpointer)->gboolean { return TRUE; }),nullptr);
    g_signal_connect(app.view,"web-process-terminated",G_CALLBACK(+[](WebKitWebView*,WebKitWebProcessTerminationReason,gpointer data) { Send(static_cast<App*>(data),"@ERROR WebKit content process terminated"); gtk_main_quit(); }),&app);
    g_signal_connect(app.view,"button-press-event",G_CALLBACK(+[](GtkWidget* widget,GdkEventButton*,gpointer data)->gboolean {
        auto* a = static_cast<App*>(data);
        auto* display = gdk_x11_display_get_xdisplay(gtk_widget_get_display(widget));
        XSetInputFocus(display,gdk_x11_window_get_xid(gtk_widget_get_window(a->plug)),RevertToParent,CurrentTime);
        gtk_widget_grab_focus(widget); return FALSE;
    }),&app);
    app.overlay=gtk_overlay_new();gtk_container_add(GTK_CONTAINER(app.plug),app.overlay);
    gtk_container_add(GTK_CONTAINER(app.overlay),GTK_WIDGET(app.view));
    app.game=gtk_gl_area_new();gtk_gl_area_set_required_version(GTK_GL_AREA(app.game),3,2);
    gtk_widget_set_halign(app.game,GTK_ALIGN_START);gtk_widget_set_valign(app.game,GTK_ALIGN_START);
    gtk_widget_set_can_focus(app.game,TRUE);gtk_widget_add_events(app.game,GDK_BUTTON_PRESS_MASK|GDK_KEY_PRESS_MASK|GDK_KEY_RELEASE_MASK);
    gtk_overlay_add_overlay(GTK_OVERLAY(app.overlay),app.game);
    g_signal_connect(app.game,"render",G_CALLBACK(Render),&app);
    g_signal_connect(app.game,"unrealize",G_CALLBACK(+[](GtkWidget* widget,gpointer data){gtk_gl_area_make_current(GTK_GL_AREA(widget));static_cast<App*>(data)->renderer.reset();}),&app);
    g_signal_connect(app.game,"button-press-event",G_CALLBACK(+[](GtkWidget* widget,GdkEventButton*,gpointer data)->gboolean{
        auto* a=static_cast<App*>(data);a->blocked=false;
        auto* display=gdk_x11_display_get_xdisplay(gtk_widget_get_display(widget));
        XSetInputFocus(display,gdk_x11_window_get_xid(gtk_widget_get_window(a->plug)),RevertToParent,CurrentTime);gtk_widget_grab_focus(widget);
        Send(a,"@INPUT {\"active\":true}");Send(a,"{\"action\":\"focus_game\"}");return TRUE;
    }),&app);
    for(auto* widget:{app.game,GTK_WIDGET(app.view)}){
        g_signal_connect(widget,"key-press-event",G_CALLBACK(Key),&app);g_signal_connect(widget,"key-release-event",G_CALLBACK(Key),&app);
    }
    g_signal_connect(app.overlay,"size-allocate",G_CALLBACK(+[](GtkWidget*,GdkRectangle*,gpointer data){Layout(static_cast<App*>(data));}),&app);
    gtk_widget_show(app.overlay);
    gtk_widget_realize(app.plug);
    gtk_widget_show(GTK_WIDGET(app.view));
    // Realize without showing the top level. The parent maps it only inside the dock.
    webkit_web_view_load_uri(app.view,app.uri.c_str());
    g_unix_fd_add(3,static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),Read,&app);
    g_timeout_add(16,+[](gpointer data)->gboolean {
        auto* a=static_cast<App*>(data);const bool active=Focused(a);
        if(active!=a->active){a->active=active;Send(a,"@INPUT "+Json{{"active",active}}.dump());}
        if(gtk_widget_get_mapped(a->game))gtk_gl_area_queue_render(GTK_GL_AREA(a->game));return G_SOURCE_CONTINUE;
    },&app);
    Send(&app,"@READY");
    gtk_main();
    // Process exit cancels pending async WebKit callbacks; App remains alive until then.
    if(app.renderer){gtk_gl_area_make_current(GTK_GL_AREA(app.game));app.renderer.reset();}
    munmap(app.shared,sizeof(reagba::SharedFrame));close(3); return 0;
}
