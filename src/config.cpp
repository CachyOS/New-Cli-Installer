#include "config.hpp"
#include "definitions.hpp"

#include <memory>

static std::unique_ptr<Config> s_config = nullptr;

bool Config::initialize() noexcept {
    if (s_config != nullptr) {
        error_inter("You should only initialize it once!\n");
        return false;
    }
    s_config = std::make_unique<Config>();
    if (s_config) {
        s_config->m_data["H_INIT"]     = "openrc";
        s_config->m_data["SYSTEM"]     = "BIOS";
        s_config->m_data["KEYMAP"]     = "us";
        s_config->m_data["XKBMAP"]     = "us";
        s_config->m_data["MOUNTPOINT"] = "/mnt";
    }

    return s_config.get();
}

auto Config::instance() -> Config* {
    return s_config.get();
}
