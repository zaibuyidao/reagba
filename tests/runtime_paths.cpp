#include "extension/RuntimePaths.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
int main() {
    namespace fs=std::filesystem;
    const auto base=fs::temp_directory_path()/("reagba-paths-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto legacy=base/"Scripts"/"zaibuyidao Scripts"/"Various"/"ReaGBA";
        auto p=reagba::ResolveRuntimePaths(base);
        if(p.web!=base/"Scripts"/"zaibuyidao Scripts"/"ReaGBA"/"web" || p.data!=base/"Data"/"ReaGBA")throw std::runtime_error("ReaPack paths mismatch");
        fs::create_directories(legacy/"data"/"states");fs::create_directories(legacy/"ROM");
        p=reagba::ResolveRuntimePaths(base);
        if(p.data!=legacy/"data" || p.roms!=legacy/"ROM")throw std::runtime_error("Legacy progress not preserved");
        fs::create_directories(base/"Data"/"ReaGBA");p=reagba::ResolveRuntimePaths(base);
        if(p.data!=base/"Data"/"ReaGBA")throw std::runtime_error("Existing new data must take precedence");
        fs::remove_all(base);std::cout<<"ReaPack assets, separate data and legacy progress paths passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';fs::remove_all(base);return 1;}
}
