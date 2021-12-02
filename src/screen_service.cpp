#include "screen_service.hpp"
#include "definitions.hpp"

#include <memory>

namespace tui {
static std::unique_ptr<screen_service> s_screen = nullptr;

bool screen_service::initialize() noexcept {
    if (s_screen != nullptr) {
        error("You should only initialize it once!\n");
        return false;
    }
    s_screen = std::make_unique<screen_service>();
    return s_screen.get();
}

auto screen_service::instance() -> screen_service* {
    return s_screen.get();
}

}  // namespace tui
