// taken from https://github.com/adrianoviana87/ltuiny
#ifndef SCREEN_SERVICE_HPP
#define SCREEN_SERVICE_HPP

//#include "view.hpp"

/* clang-format off */
//#include <memory>
#include <ftxui/component/screen_interactive.hpp>  // for ScreenInteractive
/* clang-format on */

namespace tui {
class screen_service final {
 public:
    using value_type      = ftxui::ScreenInteractive;
    using reference       = value_type&;
    using const_reference = const value_type&;

    screen_service() noexcept          = default;
    virtual ~screen_service() noexcept = default;

    static bool initialize() noexcept;
    static screen_service* instance();

    /* clang-format off */

    // Element access.
    auto data() noexcept -> reference
    { return m_screen; }
    auto data() const noexcept -> const_reference
    { return m_screen; }

    /* clang-format on */

    screen_service(const screen_service&) noexcept = delete;
    screen_service& operator=(const screen_service&) noexcept = delete;

 private:
    value_type m_screen = ftxui::ScreenInteractive::Fullscreen();
    // std::shared_ptr<tui::view> m_current_view{};
};
}  // namespace tui

#endif  // SCREEN_SERVICE_HPP
