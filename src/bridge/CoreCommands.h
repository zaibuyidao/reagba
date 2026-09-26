#pragma once
#include "app/EmulatorManager.h"
namespace reagba {
class CoreCommands {
    EmulatorManager &manager_;

  public:
    explicit CoreCommands(EmulatorManager &m) : manager_(m) {}
    void Request(const std::string &json, EmulatorManager::Reply reply);
};
} // namespace reagba
