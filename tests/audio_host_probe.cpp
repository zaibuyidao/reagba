// Test-only probe: measure the final REAPER hardware buffer after mixing.
#include "reaper_plugin.h"
#include <atomic>
#include <algorithm>
#include <cmath>
static std::atomic<int> peak[2]{};
static reaper_plugin_info_t* host=nullptr;
static int (*registerHook)(bool,audio_hook_register_t*)=nullptr;
static int Peak(int channel, bool reset) {
    int value=0;
    for(int ch=0;ch<2;++ch)if(channel<0 || channel==ch) value=std::max(value,reset ? peak[ch].exchange(0) : peak[ch].load());
    return value;
}
static void* PeakVar(void** args,int count) { return reinterpret_cast<void*>(static_cast<intptr_t>(Peak(count ? int(reinterpret_cast<intptr_t>(args[0])) : -1, count>1 && args[1]))); }
static audio_hook_register_t hook{[](bool post,int frames,double,audio_hook_register_t* info) {
    if(!post || !info->GetBuffer)return;
    for(int ch=0;ch<2;++ch)if(auto* samples=info->GetBuffer(true,ch)) {
        int value=0;
        for(int i=0;i<frames;++i)value=std::max(value,int(std::min(100.,std::abs(samples[i]))*1000000));
        int old=peak[ch].load();while(old<value && !peak[ch].compare_exchange_weak(old,value)){}
    }
},nullptr,nullptr,0,0,nullptr};
extern "C" REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE,reaper_plugin_info_t* rec) {
    static char definition[]="int\0int,bool\0channel,reset\0Test hardware output peak in millionths.\0";
    if(!rec) {
        if(registerHook)registerHook(false,&hook);
        if(host){host->Register("-API_ReaGBA_TestOutputPeak",reinterpret_cast<void*>(Peak));host->Register("-APIvararg_ReaGBA_TestOutputPeak",reinterpret_cast<void*>(PeakVar));host->Register("-APIdef_ReaGBA_TestOutputPeak",definition);}
        return 0;
    }
    host=rec;registerHook=reinterpret_cast<decltype(registerHook)>(rec->GetFunc("Audio_RegHardwareHook"));
    if(!registerHook || !registerHook(true,&hook))return 0;
    rec->Register("API_ReaGBA_TestOutputPeak",reinterpret_cast<void*>(Peak));
    rec->Register("APIvararg_ReaGBA_TestOutputPeak",reinterpret_cast<void*>(PeakVar));
    rec->Register("APIdef_ReaGBA_TestOutputPeak",definition);
    return 1;
}
