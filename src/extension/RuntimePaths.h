#pragma once
#include <filesystem>

namespace reagba {
struct RuntimePaths {
    std::filesystem::path data;
};
inline RuntimePaths ResolveRuntimePaths(const std::filesystem::path& resources) {
    const auto product=resources/"Scripts"/"zaibuyidao Scripts"/"ReaGBA";
    return {product};
}
}
