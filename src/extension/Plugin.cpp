#define REAPERAPI_IMPLEMENT
#include "reaper/ReaperAPI.h"
#include "audio/AudioEngine.h"
#include "input/InputManager.h"
#include "bridge/CoreCommands.h"
#include "extension/RuntimePaths.h"
#include "bridge/reaweb/reaweb_stream.h"
#include <chrono>
#include <cstring>
#include <limits>

namespace {
using namespace reagba;
using Clock = std::chrono::steady_clock;
reaper_plugin_info_t* plugin = nullptr;
std::vector<std::pair<std::string, void*>> registrations;
std::string lastError, output;
int nextSession = 0;

ReaWeb_ServiceHandle webService = 0;
ReaWeb_CreateFrameStreamFn createFrame = nullptr;
ReaWeb_PublishFrameFn publishFrame = nullptr;
ReaWeb_CloseStreamFn closeStream = nullptr;
ReaWeb_UnregisterServiceFn unregisterService = nullptr;
ReaWeb_CompleteServiceCallFn completeCall = nullptr;
void RegisterWebService();

// The core APIs remain usable without ReaWebAPI. Native integration is optional.
struct Session {
    int id;
    bool sdl = false;
    ReaWeb_StreamHandle video = 0;
    uint64_t videoSequence = 0;
    Frame rgba{};
    FrameBuffer::Sink sink{[](void* context, const Frame& frame) {
        auto& self = *static_cast<Session*>(context);
        for (size_t i = 0; i < frame.size(); ++i) self.rgba[i] = frame[i] | 0xff000000u;
        publishFrame(self.video, self.rgba.data(), sizeof(self.rgba), ++self.videoSequence,
                     std::chrono::duration<double>(Clock::now().time_since_epoch()).count());
    }, this};
    std::unique_ptr<EmulatorManager> manager;
    std::unique_ptr<AudioEngine> audio;
    std::unique_ptr<InputManager> input;
    std::unique_ptr<CoreCommands> commands;
    std::mutex mutex;
    std::deque<Json> replies;
    struct LargeReply { std::string text; size_t offset = 0; int window; Clock::time_point expires; };
    std::map<std::string, LargeReply> largeReplies;
    size_t largeReplyBytes = 0;
    size_t outstanding = 0;
    uint32_t held = 0, pressed = 0;
    bool fast = false, fastPressed = false, active = false, hasFrame = false;
    Clock::time_point inputTime{};
    explicit Session(int value) : id(value) {}
    ~Session() {
        // Join pending commands/battery writes before releasing their callbacks.
        if (manager) manager->Shutdown();
        if (video && closeStream) closeStream(video);
        audio.reset(); commands.reset(); input.reset(); manager.reset();
        if (sdl) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
};
std::unique_ptr<Session> session;

template<class F, class T> T Guard(F&& fn, T fallback) noexcept {
    try { lastError.clear(); return fn(); }
    catch (const std::exception& e) { lastError = e.what(); }
    catch (...) { lastError = "Unexpected GBA core error"; }
    return fallback;
}
Session& Get(int id) {
    if (!session || session->id != id) throw std::runtime_error("Invalid GBA core session");
    return *session;
}
int Create(const char* directory) {
    return Guard([&] {
        if (session) throw std::runtime_error("A GBA core session is already running");
        if (nextSession == std::numeric_limits<int>::max()) throw std::runtime_error("Session limit reached");
        const auto data = directory && *directory ? fs::u8path(directory) : ResolveRuntimePaths(fs::u8path(GetResourcePath())).data;
        if (!data.is_absolute()) throw std::runtime_error("Expected an absolute data directory");
        auto next = std::make_unique<Session>(++nextSession);
        SDL_SetMainReady();
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)) throw std::runtime_error(SDL_GetError());
        next->sdl = true;
        next->manager = std::make_unique<EmulatorManager>(data / "roms", data);
        next->audio = std::make_unique<AudioEngine>(*next->manager, plugin->GetFunc);
        next->input = std::make_unique<InputManager>();
        next->commands = std::make_unique<CoreCommands>(*next->manager);
        if (webService) {
            ReaWeb_StreamDesc desc{};
            desc.size = sizeof(desc); desc.abi_version = REAWEB_STREAM_ABI;
            desc.format = REAWEB_PIXEL_RGBA8; desc.max_bytes = sizeof(Frame); desc.capacity = 3;
            desc.width = Width; desc.height = Height; desc.stride = Width * 4;
            desc.update_rate = NativeFPS; desc.source = "reagba.emulator"; desc.owner = webService;
            if (createFrame("reagba.video", &desc, &next->video) != REAWEB_OK) throw std::runtime_error("Cannot create reagba.video");
            next->manager->frames.SetSink(&next->sink);
        }
        session = std::move(next);
        return session->id;
    }, 0);
}
bool Destroy(int id) {
    return Guard([&] { Get(id); session.reset(); return true; }, false);
}
bool Request(int id, const char* text) {
    return Guard([&] {
        auto& s = Get(id);
        if (!text || std::strlen(text) > 65536) throw std::runtime_error("Request exceeds 64 KiB");
        auto command = Json::parse(text);
        if (!command.is_object() || !command.value("action", Json()).is_string()) throw std::runtime_error("Expected an action object");
        const auto requestId = command.value("id", Json());
        if (!(requestId.is_string() || requestId.is_number_integer() || requestId.is_null())) throw std::runtime_error("Invalid request ID");
        if (s.outstanding >= 128) throw std::runtime_error("Core reply queue is full; poll replies first");
        ++s.outstanding;
        try {
            if (command["action"] == "get_audio_outputs") {
                s.audio->Update();
                std::lock_guard<std::mutex> lock(s.mutex);
                s.replies.push_back({{"type", "reply"}, {"id", requestId}, {"action", command["action"]},
                                     {"ok", true}, {"result", s.audio->Outputs()}});
                return true;
            }
            s.commands->Request(text, [&s, requestId, action = command["action"]](Json result) {
                result["type"] = "reply"; result["id"] = requestId; result["action"] = action;
                std::lock_guard<std::mutex> lock(s.mutex);
                s.replies.push_back(std::move(result));
            });
        } catch (...) { --s.outstanding; throw; }
        return true;
    }, false);
}
const char* Poll(int id) {
    return Guard([&]() -> const char* {
        auto& s = Get(id);
        Json result;
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            if (s.replies.empty()) return "";
            result = std::move(s.replies.front()); s.replies.pop_front();
        }
        --s.outstanding;
        s.audio->Update();
        if (result.value("ok", false) && result.contains("result") && result["result"].is_object() && result["result"].contains("loaded"))
            result["result"].update(s.audio->Status());
        output = result.dump();
        if (output.size() > 8 * 1024 * 1024) {
            result.erase("result"); result["ok"] = false; result["error"] = "Core result exceeds 8 MiB";
            output = result.dump();
        }
        return output.c_str();
    }, static_cast<const char*>(""));
}
const char* ReadFrame(int id, bool force) {
    return Guard([&]() -> const char* {
        auto& s = Get(id);
        const bool fresh = s.manager->frames.Consume();
        s.hasFrame = s.hasFrame || fresh;
        if (!fresh && !(force && s.hasFrame)) return "";
        const auto& frame = s.manager->frames.Front();
        const auto* bytes = reinterpret_cast<const unsigned char*>(frame.data());
        size_t length = sizeof(frame);
        // Lossless runs avoid routing large mostly-flat GBA frames through the
        // runtime's large-message worker. No renderer or browser dependency.
        std::vector<unsigned char> runs;
        runs.reserve(4096);
        for (size_t start = 0; start < frame.size() && runs.size() < sizeof(frame);) {
            size_t end = start + 1;
            while (end < frame.size() && frame[end] == frame[start] && end - start < 65535) ++end;
            const auto count = end - start;
            runs.push_back(static_cast<unsigned char>(count));
            runs.push_back(static_cast<unsigned char>(count >> 8));
            runs.insert(runs.end(), bytes + start * 4, bytes + start * 4 + 4);
            start = end;
        }
        const bool compressed = runs.size() < sizeof(frame);
        if (compressed) { bytes = runs.data(); length = runs.size(); }
        static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        // Both complete RGBA frames and six-byte runs are divisible by three.
        output = "{\"type\":\"frame\",\"width\":240,\"height\":160,\"encoding\":\"";
        output += compressed ? "rle-rgba" : "rgba";
        output += "\",\"rgba\":\"";
        output.reserve(length / 3 * 4 + 100);
        for (size_t i = 0; i < length; i += 3) {
            const unsigned v = (unsigned(bytes[i]) << 16) | (unsigned(bytes[i+1]) << 8) | bytes[i+2];
            output += alphabet[(v >> 18) & 63]; output += alphabet[(v >> 12) & 63];
            output += alphabet[(v >> 6) & 63]; output += alphabet[v & 63];
        }
        output += "\"}";
        return output.c_str();
    }, static_cast<const char*>(""));
}
bool SetInput(int id, int mask, bool fast, bool active) {
    return Guard([&] {
        auto& s = Get(id);
        if (mask < 0 || mask > 1023) throw std::runtime_error("Invalid GBA input mask");
        s.inputTime = Clock::now(); s.active = active;
        if (!active) { s.held = s.pressed = 0; s.fast = s.fastPressed = false; }
        else {
            s.pressed |= unsigned(mask) & ~s.held; s.held = unsigned(mask);
            s.fastPressed = s.fastPressed || (fast && !s.fast); s.fast = fast;
        }
        return true;
    }, false);
}
void Tick() {
    RegisterWebService();
    if (!session) return;
    auto& s = *session;
    s.audio->Update();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        for (auto it = s.largeReplies.begin(); it != s.largeReplies.end();)
            if (Clock::now() >= it->second.expires) { s.largeReplyBytes -= it->second.text.size(); it = s.largeReplies.erase(it); }
            else ++it;
    }
    // A terminated/reloaded frontend must never leave a game key held.
    const bool active = s.active && Clock::now() - s.inputTime < std::chrono::milliseconds(750);
    auto mask = s.input->Poll(active) | (active ? s.held | s.pressed : 0);
    if ((mask & 0x30) == 0x30) mask &= ~0x30u;
    if ((mask & 0xc0) == 0xc0) mask &= ~0xc0u;
    s.manager->SetInput(mask);
    s.manager->SetFastForward(active && (s.fast || s.fastPressed));
    s.pressed = 0; s.fastPressed = false;
}
int WebRequest(void*, uint64_t handle, uint64_t request, int window, const char* method, const char* payload) {
    try {
        const auto name = std::string(method);
        auto command = Json::parse(payload);
        if (command.is_null()) command = Json::object();
        if (!command.is_object()) return REAWEB_INVALID_ARGUMENT;
        if (name == "input") {
            if (!session) return REAWEB_SERVICE_ERROR;
            const auto mask = command.value("mask", 0);
            const auto fast = command.value("fast", false), active = command.value("active", true);
            if (!SetInput(session->id, mask, fast, active)) return REAWEB_INVALID_ARGUMENT;
            session->pressed = 0; session->fastPressed = false;
            auto keys = active ? unsigned(mask) : 0;
            if ((keys & 0x30) == 0x30) keys &= ~0x30u;
            if ((keys & 0xc0) == 0xc0) keys &= ~0xc0u;
            session->manager->SetInput(keys);
            session->manager->SetFastForward(active && fast);
            return REAWEB_OK;
        }
        if (!request) return REAWEB_INVALID_ARGUMENT;
        if (!session && !Create(command.value("dataDirectory", std::string()).c_str())) {
            completeCall(handle, request, nullptr, REAWEB_SERVICE_ERROR, lastError.c_str());
            return REAWEB_OK;
        }
        if (!session->video) {
            completeCall(handle, request, nullptr, REAWEB_SERVICE_ERROR, "Close the legacy core session before native Stream attachment");
            return REAWEB_OK;
        }
        if (name == "readResult") {
            Json part;
            {
                std::lock_guard<std::mutex> lock(session->mutex);
                auto it = session->largeReplies.find(command.at("token").get<std::string>());
                if (it == session->largeReplies.end() || it->second.window != window || Clock::now() >= it->second.expires)
                    throw std::runtime_error("Result expired or unavailable");
                auto& result = it->second;
                if (command.at("offset").get<size_t>() != result.offset) throw std::runtime_error("Invalid result offset");
                size_t end = std::min(result.text.size(), result.offset + 128 * 1024);
                while (end < result.text.size() && (static_cast<unsigned char>(result.text[end]) & 0xc0) == 0x80) --end;
                part = {{"text", result.text.substr(result.offset, end - result.offset)}, {"offset", result.offset}, {"bytes", end - result.offset}};
                result.offset = end;
                if (end == result.text.size()) { session->largeReplyBytes -= result.text.size(); session->largeReplies.erase(it); }
            }
            return completeCall(handle, request, part.dump().c_str(), REAWEB_OK, nullptr);
        }
        static const std::map<std::string, std::string> methods{
            {"loadRom", "load_rom"}, {"closeRom", "stop"}, {"resume", "start"},
            {"saveState", "save_state"}, {"loadState", "load_state"}, {"getState", "get_emulator_state"},
            {"setSpeed", "set_speed"}, {"getSettings", "get_settings"}, {"setSettings", "set_settings"}};
        const auto alias = methods.find(name);
        command["action"] = name == "settings" ? (command.contains("settings") ? "set_settings" : "get_settings") :
                            alias == methods.end() ? name : alias->second;
        session->audio->Update();
        if (command["action"] == "get_audio_outputs")
            return completeCall(handle, request, session->audio->Outputs().dump().c_str(), REAWEB_OK, nullptr);
        const auto audioStatus = session->audio->Status();
        session->commands->Request(command.dump(), [handle, request, window, audioStatus, owner = session.get()](Json reply) {
            if (reply.value("ok", false)) {
                auto result = reply.value("result", Json());
                if (result.is_object() && result.contains("loaded")) result.update(audioStatus);
                auto encoded = result.dump();
                if (encoded.size() > 8 * 1024 * 1024) {
                    completeCall(handle, request, nullptr, REAWEB_SERVICE_ERROR, "Core result exceeds 8 MiB"); return;
                }
                if (encoded.size() > 900 * 1024) {
                    const auto token = std::to_string(owner->id) + "." + std::to_string(request);
                    const auto bytes = encoded.size();
                    {
                        std::lock_guard<std::mutex> lock(owner->mutex);
                        if (owner->largeReplies.size() >= 4 || owner->largeReplyBytes + bytes > 16 * 1024 * 1024) {
                            completeCall(handle, request, nullptr, REAWEB_QUEUE_LIMIT, "Large result queue is full"); return;
                        }
                        owner->largeReplies.emplace(token, Session::LargeReply{std::move(encoded), 0, window, Clock::now() + std::chrono::seconds(60)});
                        owner->largeReplyBytes += bytes;
                    }
                    encoded = Json{{"__reagbaResult", {{"token", token}, {"bytes", bytes}}}}.dump();
                }
                completeCall(handle, request, encoded.c_str(), REAWEB_OK, nullptr);
            } else completeCall(handle, request, nullptr, REAWEB_SERVICE_ERROR, reply.value("error", std::string("Core request failed")).c_str());
        });
        return REAWEB_OK;
    } catch (const std::exception& error) {
        if (request) { completeCall(handle, request, nullptr, REAWEB_INVALID_ARGUMENT, error.what()); return REAWEB_OK; }
        return REAWEB_INVALID_ARGUMENT;
    }
}
void RegisterWebService() {
    if (webService || !plugin) return;
    auto registerService = reinterpret_cast<ReaWeb_RegisterServiceFn>(plugin->GetFunc("ReaWeb_RegisterService"));
    auto setInput = reinterpret_cast<ReaWeb_SetServiceInputFn>(plugin->GetFunc("ReaWeb_SetServiceInput"));
    auto setShutdown = reinterpret_cast<ReaWeb_SetServiceShutdownFn>(plugin->GetFunc("ReaWeb_SetServiceShutdown"));
    createFrame = reinterpret_cast<ReaWeb_CreateFrameStreamFn>(plugin->GetFunc("ReaWeb_CreateFrameStream"));
    publishFrame = reinterpret_cast<ReaWeb_PublishFrameFn>(plugin->GetFunc("ReaWeb_PublishFrame"));
    closeStream = reinterpret_cast<ReaWeb_CloseStreamFn>(plugin->GetFunc("ReaWeb_CloseStream"));
    unregisterService = reinterpret_cast<ReaWeb_UnregisterServiceFn>(plugin->GetFunc("ReaWeb_UnregisterService"));
    completeCall = reinterpret_cast<ReaWeb_CompleteServiceCallFn>(plugin->GetFunc("ReaWeb_CompleteServiceCall"));
    if (!registerService || !setInput || !setShutdown || !createFrame || !publishFrame || !closeStream || !unregisterService || !completeCall) return;
    ReaWeb_ServiceCallbacks callbacks{sizeof(ReaWeb_ServiceCallbacks), REAWEB_SERVICE_ABI, nullptr, WebRequest, nullptr};
    if (registerService("reagba", &callbacks, &webService) != REAWEB_OK) { webService = 0; return; }
    setInput(webService, "input");
    setShutdown(webService, [](void*) { session.reset(); webService = 0; });
}
const char* CoreError() { return lastError.c_str(); }
int Arg(void** a, int n, int i) { return n > i ? static_cast<int>(reinterpret_cast<intptr_t>(a[i])) : 0; }
void* CreateVar(void** a, int n) { return reinterpret_cast<void*>(static_cast<intptr_t>(Create(n ? static_cast<const char*>(a[0]) : ""))); }
void* DestroyVar(void** a, int n) { return reinterpret_cast<void*>(static_cast<intptr_t>(Destroy(Arg(a,n,0)))); }
void* RequestVar(void** a, int n) { return reinterpret_cast<void*>(static_cast<intptr_t>(Request(Arg(a,n,0), n > 1 ? static_cast<const char*>(a[1]) : nullptr))); }
void* PollVar(void** a, int n) { return const_cast<char*>(Poll(Arg(a,n,0))); }
void* FrameVar(void** a, int n) { return const_cast<char*>(ReadFrame(Arg(a,n,0), Arg(a,n,1) != 0)); }
void* InputVar(void** a, int n) { return reinterpret_cast<void*>(static_cast<intptr_t>(SetInput(Arg(a,n,0),Arg(a,n,1),Arg(a,n,2)!=0,Arg(a,n,3)!=0))); }
void* ErrorVar(void**, int) { return const_cast<char*>(CoreError()); }
void Add(const std::string& name, void* value) {
    if (!plugin->Register(name.c_str(), value)) throw std::runtime_error("Cannot register " + name);
    registrations.emplace_back(name, value);
}
void API(const char* name, void* fn, void* vararg, const char* definition) {
    Add(std::string("API_")+name, fn); Add(std::string("APIvararg_")+name, vararg);
    Add(std::string("APIdef_")+name, const_cast<char*>(definition));
}
void Unload() {
    if (plugin) for (auto i = registrations.rbegin(); i != registrations.rend(); ++i) plugin->Register(("-"+i->first).c_str(), i->second);
    registrations.clear();
    session.reset();
    if (webService && unregisterService) unregisterService(webService);
    webService = 0; plugin = nullptr;
}
}
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE, reaper_plugin_info_t* rec) {
    if (!rec) { Unload(); return 0; }
    if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc || REAPERAPI_LoadAPI(rec->GetFunc)) return 0;
    plugin = rec;
    try {
        API("ReaGBA_Create", reinterpret_cast<void*>(Create), reinterpret_cast<void*>(CreateVar),
            "int\0const char*\0dataDirectory\0Create a headless GBA session. Empty directory uses existing ReaGBA data. Zero on failure.\0");
        API("ReaGBA_Destroy", reinterpret_cast<void*>(Destroy), reinterpret_cast<void*>(DestroyVar),
            "bool\0int\0sessionId\0Stop the core, save battery data and release audio and input.\0");
        API("ReaGBA_Request", reinterpret_cast<void*>(Request), reinterpret_cast<void*>(RequestVar),
            "bool\0int,const char*\0sessionId,json\0Queue a core command; use Poll for the asynchronous reply.\0");
        API("ReaGBA_Poll", reinterpret_cast<void*>(Poll), reinterpret_cast<void*>(PollVar),
            "const char*\0int\0sessionId\0Read one JSON reply without blocking; empty when no reply.\0");
        API("ReaGBA_ReadFrame", reinterpret_cast<void*>(ReadFrame), reinterpret_cast<void*>(FrameVar),
            "const char*\0int,bool\0sessionId,force\0Read the latest RGBA frame as base64 JSON. Empty when unchanged, unless force.\0");
        API("ReaGBA_SetInput", reinterpret_cast<void*>(SetInput), reinterpret_cast<void*>(InputVar),
            "bool\0int,int,bool,bool\0sessionId,mask,fastForward,active\0Update GBA button bits 0-9 and foreground state. Refresh within 750 ms.\0");
        API("ReaGBA_GetLastError", reinterpret_cast<void*>(CoreError), reinterpret_cast<void*>(ErrorVar),
            "const char*\0\0\0Get the last synchronous core API error.\0");
        Add("timer", reinterpret_cast<void*>(Tick));
        return 1;
    } catch (...) { Unload(); return 0; }
}
