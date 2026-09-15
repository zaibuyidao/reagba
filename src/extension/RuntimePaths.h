#pragma once
#include <filesystem>

namespace reagba {
struct RuntimePaths {
    std::filesystem::path product, web, data, roms;
};
inline RuntimePaths ResolveRuntimePaths(const std::filesystem::path& resources) {
    namespace fs=std::filesystem;
    const auto scripts=resources/"Scripts"/"zaibuyidao Scripts";
    const auto product=scripts/"ReaGBA",legacy=scripts/"Various"/"ReaGBA";
    auto data=resources/"Data"/"ReaGBA";
    // Preserve pre-ReaPack progress in place. Fresh installs keep writable data
    // outside the author's publishing directory and its managed web assets.
    if(!fs::exists(data) && fs::is_directory(legacy/"data"))data=legacy/"data";
    auto roms=resources/"Data"/"ReaGBA"/"ROM";
    if(fs::is_directory(legacy/"ROM"))roms=legacy/"ROM";
    return {product,product/"web",data,roms};
}
}
