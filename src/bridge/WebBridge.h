#pragma once
#include "app/EmulatorManager.h"
namespace reagba {
class WebBridge {
    EmulatorManager &manager_;

  public:
    explicit WebBridge(EmulatorManager &m) : manager_(m) {}
    void Request(const std::string &json, EmulatorManager::Reply reply);
};
} // namespace reagba
