#pragma once

#include <algorithm>    // for find, contains
#include <iterator>     // for make_move_iterator
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move
#include <vector>       // for vector

namespace gucc::detail {

struct InitcpioArrays {
    std::vector<std::string> modules;
    std::vector<std::string> files;
    std::vector<std::string> hooks;
};

[[nodiscard]] auto parse_initcpio(std::string_view content) noexcept -> InitcpioArrays;

[[nodiscard]] auto modify_initcpio_line(std::string_view line, std::string_view modules, std::string_view files, std::string_view hooks) noexcept -> std::string;

[[nodiscard]] auto rewrite_initcpio(std::string_view content, std::string_view modules, std::string_view files, std::string_view hooks) noexcept -> std::string;

class Initcpio final {
 public:
    explicit Initcpio(std::string_view file_path) noexcept
      : m_file_path(file_path) { }

    bool parse_file() noexcept;
    [[nodiscard]] bool write() const noexcept;

    [[nodiscard]] auto arrays() const noexcept -> const InitcpioArrays& { return m_arrays; }
    [[nodiscard]] auto arrays() noexcept -> InitcpioArrays& { return m_arrays; }

    bool append_module(std::string&& module) noexcept { return append_to(&InitcpioArrays::modules, std::move(module)); }
    bool append_file(std::string&& file) noexcept { return append_to(&InitcpioArrays::files, std::move(file)); }
    bool append_hook(std::string&& hook) noexcept { return append_to(&InitcpioArrays::hooks, std::move(hook)); }
    bool append_hooks(std::vector<std::string>&& new_hooks) noexcept { return insert_hooks(std::nullopt, std::move(new_hooks)); }

    bool insert_hook(std::string_view needle, std::string&& hook) noexcept { return insert_hooks(needle, {std::move(hook)}); }
    bool insert_hook(std::string_view needle, std::vector<std::string>&& new_hooks) noexcept { return insert_hooks(needle, std::move(new_hooks)); }

    bool remove_module(std::string_view module) noexcept { return remove_from(&InitcpioArrays::modules, module); }
    bool remove_hook(std::string_view hook) noexcept { return remove_from(&InitcpioArrays::hooks, hook); }

 private:
    using Field = std::vector<std::string> InitcpioArrays::*;

    [[nodiscard]] auto read_config() const noexcept -> std::optional<std::string>;

    bool append_to(Field field, std::string&& item) noexcept {
        if (!parse_file() || std::ranges::contains(m_arrays.*field, item)) {
            return false;
        }
        (m_arrays.*field).emplace_back(std::move(item));
        return write();
    }

    bool remove_from(Field field, std::string_view item) noexcept {
        if (!parse_file()) {
            return false;
        }
        if (std::erase(m_arrays.*field, item) == 0) {
            return false;
        }
        return write();
    }

    bool insert_hooks(std::optional<std::string_view> before, std::vector<std::string> new_hooks) noexcept {
        if (!parse_file()) {
            return false;
        }
        auto& hooks = m_arrays.hooks;
        std::erase_if(new_hooks, [&hooks](const auto& hook) { return std::ranges::contains(hooks, hook); });
        if (new_hooks.empty()) {
            return false;
        }
        const auto pos = before ? std::ranges::find(hooks, *before) : hooks.end();
        hooks.insert(pos, std::make_move_iterator(new_hooks.begin()), std::make_move_iterator(new_hooks.end()));
        return write();
    }

    std::string_view m_file_path{};
    InitcpioArrays m_arrays{};
};

}  // namespace gucc::detail
