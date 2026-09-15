#include "extension/RuntimePaths.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
int main() {
    namespace fs=std::filesystem;
    const auto base=fs::temp_directory_path()/("reagba-paths-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto product=base/"Scripts"/"zaibuyidao Scripts"/"ReaGBA";
        const auto p=reagba::ResolveRuntimePaths(base);
        if(p.product!=product || p.web!=product/"web" || p.data!=product || p.roms!=product/"ROM")
            throw std::runtime_error("Author product paths mismatch");
        fs::create_directories(base/"Data"/"ReaGBA"/"ROM");
        fs::create_directories(base/"Scripts"/"zaibuyidao Scripts"/"Various"/"ReaGBA"/"data");
        const auto unchanged=reagba::ResolveRuntimePaths(base);
        if(unchanged.data!=product || unchanged.roms!=product/"ROM")
            throw std::runtime_error("Legacy paths must not be used");
        fs::remove_all(base);std::cout<<"Author product data paths passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';fs::remove_all(base);return 1;}
}
