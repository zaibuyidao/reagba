#include "input/InputManager.h"
#include "input/KeyBindings.h"
#include "platform/Windows/Keyboard.h"
#include "core/gba/GBACore.h"
#include "save/SaveManager.h"
#include <iostream>
using namespace reagba;
static void require(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
int wmain(int argc, wchar_t **argv) {
    try {
        SDL_SetMainReady();
        require(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0, "SDL init");
        Json report = {{"sdl_video_initialized", bool(SDL_WasInit(SDL_INIT_VIDEO))}};
        {
            InputManager input;
            Json config = Json::object();
            NormalizeKeys(config);
            input.Configure(config);
            const int vks[] = {'J','K',VK_SPACE,VK_RETURN,'D','A','W','S','Q','O','L'};
            for (size_t i=0; i<11; ++i) require(VirtualKey(input.Mapping()[i]) == vks[i], "native key translation");
            for (int i=0; i<10; ++i) {
                auto key=input.Mapping()[i];
                input.Key(key,true);
                require(input.Poll(true)==(1u<<i), "key down maps to correct GBA button");
                input.Key(key,false);
                require(input.Poll(true)==0, "key released");
                input.Key(key,true); input.Key(key,false);
                require(input.Poll(true)==(1u<<i), "short tap retained for one poll");
                require(input.Poll(true)==0, "short tap released after poll");
            }
            input.Key(SDL_SCANCODE_W,true); input.Key(SDL_SCANCODE_J,true);
            require(input.Poll(true)==65, "movement plus action chord");
            require(input.Poll(false)==0 && input.Poll(true)==0, "focus loss clears all keys");
            input.Key(SDL_SCANCODE_L,true);
            require(input.Poll(true)==0 && input.FastForward(), "L boosts without GBA shoulder input");
            input.Key(SDL_SCANCODE_L,false); input.Poll(true);
            require(!input.FastForward(), "boost releases");
            config["keys"][0]="F"; input.Configure(config);
            input.Key(SDL_SCANCODE_F,true); require(input.Poll(true)==1, "custom mapping");
            config["fast_forward_key"]="R";input.Configure(config);
            input.Key(SDL_SCANCODE_R,true);require(input.Poll(true)==0 && input.FastForward(),"custom boost mapping");
            input.Key(SDL_SCANCODE_R,false);input.Poll(true);require(!input.FastForward(),"custom boost releases");
            report["mapping_and_focus_passed"]=true;
            report["default_keys"]=DefaultKeys();
            if(argc>1) {
                GBACore core; core.LoadROM(fs::path(argv[1]));
                for(int i=0;i<10;++i) {
                    core.SetInput(1u<<i);
                    require(core.ReadInput()==(1u<<i), "GBA KEYINPUT register receives key");
                    core.RunFrame(); core.DrainAudio();
                    require(core.ReadInput()==(1u<<i), "GBA KEYINPUT held across frame");
                    core.SetInput(0); require(core.ReadInput()==0, "GBA KEYINPUT releases");
                }
                report["all_ten_gba_register_bits_passed"]=true;
            }
            report["passed"]=true;
            if(argc>2) WriteJSON(fs::path(argv[2]),report);
            std::cout<<report.dump(2)<<'\n';
        }
        SDL_Quit(); return 0;
    } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
