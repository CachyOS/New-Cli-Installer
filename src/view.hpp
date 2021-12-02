// taken from https://github.com/adrianoviana87/ltuiny
#ifndef VIEW_HPP
#define VIEW_HPP

/* clang-format off */
#include <functional>
#include <ftxui/component/component.hpp>
/* clang-format on */

namespace tui {
class view : public ftxui::Component {
 public:
    view()          = default;
    virtual ~view() = default;

    void initialize() { initialize_ui(); };
    void set_on_close(std::function<void()>&& val) { on_close = std::move(val); }

 protected:
    void close() { on_close(); }
    virtual void initialize_ui() = 0;

 private:
    std::function<void()> on_close;
};
}  // namespace tui

#endif  // VIEW_HPP
