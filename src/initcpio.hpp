#ifndef INITCPIO_HPP
#define INITCPIO_HPP

#include <algorithm>    // for find
#include <string_view>  // for string_view
#include <string>       // for string
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

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace detail {

class Initcpio {
 public:
    Initcpio(const std::string_view& file_path) noexcept : m_file_path(file_path) {}
    
    bool parse_file() noexcept;

    inline bool append_module(std::string&& module) noexcept {
        if (!this->parse_file()) { return false; }
        if (ranges::contains(modules, module)) { return false; }

        modules.emplace_back(std::move(module));
        return this->write();
    }
    inline bool append_file(std::string&& file) noexcept {
        if (!this->parse_file()) { return false; }
        if (ranges::contains(files, file)) { return false; }

        files.emplace_back(std::move(file));
        return this->write();
    }
    inline bool append_hook(std::string&& hook) noexcept {
        if (!this->parse_file()) { return false; }
        if (ranges::contains(hooks, hook)) { return false; }

        hooks.emplace_back(std::move(hook));
        return this->write();
    }
    inline bool insert_hook(std::string&& needle, std::string&& hook) noexcept {
        if (!this->parse_file()) { return false; }
        const auto& needle_pos = std::find(hooks.begin(), hooks.end(), std::move(needle));
        hooks.insert(needle_pos, hook);
        return this->write();
    }
    inline bool insert_hook(std::string&& needle, std::vector<std::string>&& hook) noexcept {
        if (!this->parse_file()) { return false; }
        const auto& needle_pos = std::find(hooks.begin(), hooks.end(), std::move(needle));
        hooks.insert(needle_pos, hook.begin(), hook.end());
        return this->write();
    }

    inline bool remove_module(std::string&& module) noexcept {
        if (!this->parse_file()) { return false; }
        const auto& needle_pos = std::find(modules.begin(), modules.end(), std::move(module));
        if (needle_pos == modules.end()) { return false; }

        modules.erase(needle_pos);
        return this->write();
    }
    inline bool remove_hook(std::string&& hook) noexcept {
        if (!this->parse_file()) { return false; }
        const auto& needle_pos = std::find(hooks.begin(), hooks.end(), std::move(hook));
        if (needle_pos == hooks.end()) { return false; }

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
