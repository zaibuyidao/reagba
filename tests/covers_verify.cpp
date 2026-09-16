#include "rom/CoverManager.h"
#include "save/SaveManager.h"
#include <iostream>
#include <future>
#include <ctime>
#include <stdexcept>
using namespace reagba;
static void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static Json Wait(CoverManager &covers, const std::string &code, int seconds = 5) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end) {
        auto result = covers.Get(code);
        if (result.at("status") != "pending") return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Cover worker did not finish");
}
static std::vector<uint8_t> PNG() {
    // A complete 1x1 PNG, including IDAT, CRCs and IEND.
    const std::string hex="89504e470d0a1a0a0000000d4948445200000001000000010804000000b51c0c020000000b4944415478da6364f80f00010501012718e3660000000049454e44ae426082";
    std::vector<uint8_t> bytes;
    for (size_t i=0;i<hex.size();i+=2) bytes.push_back(uint8_t(std::stoul(hex.substr(i,2),nullptr,16)));
    return bytes;
}
static std::vector<uint8_t> Index() {
    const std::string text="game (\n name \"Example & Game (USA)\"\n serial \"ABCE\"\n)\n"
        "game (\n name \"Missing (Japan)\"\n serial \"ABCJ\"\n)\n"
        "game (\n name \"Failure (USA)\"\n serial \"FAIL\"\n)\n"
        "game (\n name \"Corrupt (USA)\"\n serial \"BAD0\"\n)\n";
    return {text.begin(),text.end()};
}
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--online") {
            CoverManager covers(fs::current_path()/"verification"/"covers-online");
            covers.Enable(true);
            auto result = Wait(covers, argc > 2 ? argv[2] : "BZME", 90);
            Require(result.at("status")=="ready","Live source did not return a cover");
            std::cout << "PASS: real serial lookup and HTTPS PNG download (" << result.at("image").get<std::string>().size() << " data URI bytes)\n";
            return 0;
        }
        const auto root=fs::temp_directory_path()/("reagba-covers-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::atomic<int> requests{0};
        std::vector<uint8_t> rom(192,0);
        rom[0xb2]=0x96;
        rom[0xac]='A';rom[0xad]='B';rom[0xae]='C';rom[0xaf]='E';
        AtomicWrite(root/"renamed game.gba",rom);
        Require(InspectROM(root/"renamed game.gba",false).code=="ABCE","Renamed ROM did not expose its internal serial");
        CoverFetch fetch = [&](const std::string &path, size_t, const CoverCancel &) -> CoverDownload {
            ++requests;
            if (path.find(".dat")!=std::string::npos) return {200,Index()};
            if (path.find("Example%20_%20Game%20%28USA%29.png")!=std::string::npos) return {200,PNG()};
            if (path.find("Failure")!=std::string::npos) throw std::runtime_error("Offline");
            if (path.find("Corrupt")!=std::string::npos) return {200,{'n','o','t','p','n','g'}};
            return {404,{}};
        };
        {
            CoverManager covers(root,fetch);
            Require(Wait(covers,"ABCE").at("status")=="disabled","Disabled setting ignored");
            Require(requests==0,"Default cover lookup used the network");
            covers.Enable(true);
            auto found=Wait(covers,"ABCE");
            Require(found.at("status")=="ready","Serial lookup or escaped artwork name failed");
            Require(found.at("image").get<std::string>().find("data:image/png;base64,iVBORw0KGgo")==0,"PNG not delivered through data URI");
            Require(requests==2,"Unexpected metadata/image requests");
            Require(Wait(covers,"ABCE").at("status")=="ready" && requests==2,"Cached image downloaded again");
            Require(Wait(covers,"ABCJ").at("status")=="missing","404 not treated as missing");
            Require(Wait(covers,"NONE").at("status")=="missing","Unknown serial not treated as missing");
            Require(Wait(covers,"FAIL").at("status")=="error","Network failure did not degrade gracefully");
            Require(Wait(covers,"BAD0").at("status")=="error","Invalid image accepted");
            const auto count=requests.load();
            for(const auto *code:{"ABCJ","NONE","FAIL","BAD0"}) Wait(covers,code);
            Require(requests==count,"Negative cache did not suppress retries");
            covers.Enable(false);
            Require(Wait(covers,"ABCE").at("status")=="ready","Disabling downloads hid cached artwork");
            Require(Wait(covers,"XXXX").at("status")=="disabled" && requests==count,"Disabled lookup went online");
            for(const auto *code:{"../../", "aBCE", "A/B1", "", "TOOLONG"})
                Require(Wait(covers,code).at("status")=="missing","Unsafe or invalid serial accepted");
        }
        {
            const auto count=requests.load();
            CoverManager covers(root,fetch);
            Require(Wait(covers,"ABCE").at("status")=="ready" && requests==count,"Offline cache did not survive restart");
            covers.Enable(true);
            Require(Wait(covers,"ABCJ").at("status")=="missing" && requests==count,"Negative cache did not survive restart");
            AtomicWrite(root/"ABCE.png",{'b','a','d'});
            Require(Wait(covers,"ABCE").at("status")=="ready" && requests==count+1,"Corrupt cache not repaired from cached index");
        }
        {
            const auto repair=root/"repair";
            AtomicWrite(repair/"gba-index.dat",{'b','a','d'});
            WriteJSON(repair/"ABCE.json",{{"status","pending"},{"retry_after",std::time(nullptr)+3600}});
            CoverManager covers(repair,fetch);
            covers.Enable(true);
            Require(Wait(covers,"ABCE").at("status")=="ready","Malformed index or status cache prevented recovery");
        }
        // A blocked downloader must neither block Get nor survive disabling the setting.
        std::promise<void> entered;
        {
            CoverManager covers(root/"cancel",[&](const std::string &,size_t,const CoverCancel &cancel)->CoverDownload {
                entered.set_value();
                while (!cancel()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                throw std::runtime_error("Cancelled");
            });
            covers.Enable(true);
            const auto start=std::chrono::steady_clock::now();
            Require(covers.Get("ABCE").at("status")=="pending","Lookup was synchronous");
            Require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100),"Lookup blocked emulator thread");
            Require(entered.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready,"Downloader did not start");
            covers.Enable(false);
            Require(Wait(covers,"ABCE").at("status")=="disabled","In-flight download not cancelled");
        }
        fs::remove_all(root);
        std::cout << "PASS: serial matching, bounded background IO, opt-in, cancellation, PNG validation, persistent cache, failures and retry limits\n";
        return 0;
    } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
