#ifndef INITCPIO_HPP
#define INITCPIO_HPP

#include <algorithm>    // for find
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/contains.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace detail {

class Initcpio {
 public:
    Initcpio(const std::string_view& file_path) noexcept : m_file_path(file_path) { }

    bool parse_file() noexcept;

    inline bool append_module(std::string&& module) noexcept {
        /* clang-format off */
        if (!this->parse_file()) { return false; }
        if (ranges::contains(modules, module)) { return false; }
        /* clang-format on */

        modules.emplace_back(std::move(module));
        return this->write();
    }
    inline bool append_file(std::string&& file) noexcept {
        /* clang-format off */
        if (!this->parse_file()) { return false; }
        if (ranges::contains(files, file)) { return false; }
        /* clang-format on */

        files.emplace_back(std::move(file));
        return this->write();
    }
    inline bool append_hook(std::string&& hook) noexcept {
        /* clang-format off */
        if (!this->parse_file()) { return false; }
        if (ranges::contains(hooks, hook)) { return false; }
        /* clang-format on */

        hooks.emplace_back(std::move(hook));
        return this->write();
    }
    inline bool append_hooks(std::vector<std::string>&& hook) noexcept {
        /* clang-format off */
        if (!this->parse_file()) { return false; }
        auto&& filtered_input = hook | ranges::views::filter([&](auto&& el) {
            return !ranges::contains(hooks, el);
        }) | ranges::to<std::vector<std::string>>();
        if (filtered_input.empty()) { return false; }

        hooks.insert(hooks.end(), filtered_input.begin(), filtered_input.end());
        return this->write();
    }
    inline bool insert_hook(std::string&& needle, std::string&& hook) noexcept {
        /* clang-format off */
        if (!this->parse_file()) { return false; }
        if (ranges::contains(hooks, hook)) { return false; }
        /* clang-format on */

        const auto& needle_pos = std::find(hooks.begin(), hooks.end(), std::move(needle));
        hooks.insert(needle_pos, hook);
        return this->write();
    }
    inline bool insert_hook(std::string&& needle, std::vector<std::string>&& hook) noexcept {
        /* clang-format off */
        if (!this->parse_file()) { return false; }
        auto&& filtered_input = hook | ranges::views::filter([&](auto&& el) {
            return !ranges::contains(hooks, el);
        }) | ranges::to<std::vector<std::string>>();
        if (filtered_input.empty()) { return false; }
        /* clang-format on */

        const auto& needle_pos = std::find(hooks.begin(), hooks.end(), std::move(needle));
        hooks.insert(needle_pos, filtered_input.begin(), filtered_input.end());
        return this->write();
    }

    inline bool remove_module(std::string&& module) noexcept {
        /* clang-format off */
        if (!this->parse_file()) { return false; }
        const auto& needle_pos = std::find(modules.begin(), modules.end(), std::move(module));
        if (needle_pos == modules.end()) { return false; }
        /* clang-format on */

        modules.erase(needle_pos);
        return this->write();
    }
    inline bool remove_hook(std::string&& hook) noexcept {
        /* clang-format off */
        if (!this->parse_file()) { return false; }
        const auto& needle_pos = std::find(hooks.begin(), hooks.end(), std::move(hook));
        if (needle_pos == hooks.end()) { return false; }
        /* clang-format on */

        hooks.erase(needle_pos);
        return this->write();
    }

    bool write() const noexcept;

    std::vector<std::string> modules{};
    std::vector<std::string> files{};
    std::vector<std::string> hooks{};

 private:
    std::string_view m_file_path{};
};

}  // namespace detail

#endif  // INITCPIO_HPP
