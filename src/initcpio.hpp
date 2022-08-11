#ifndef INITCPIO_HPP
#define INITCPIO_HPP

#include <string_view>  // for string_view
#include <string>       // for string
#include <vector>       // for vector

namespace detail {

class Initcpio {
 public:
    Initcpio(const std::string_view& file_path) noexcept : m_file_path(file_path) {}
    
    bool parse_file() noexcept;

    inline bool append_module(std::string&& module) noexcept {
        if (!this->parse_file()) { return false; }
        modules.emplace_back(std::move(module));
        return this->write();
    }
    inline bool append_hook(std::string&& hook) noexcept {
        if (!this->parse_file()) { return false; }
        hooks.emplace_back(std::move(hook));
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
