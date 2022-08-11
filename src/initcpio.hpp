#ifndef INITCPIO_HPP
#define INITCPIO_HPP

#include <string_view>  // for string_view
#include <string>       // for string
#include <vector>       // for vector

namespace detail {

class Initcpio {
 public:
    Initcpio(const std::string_view& file_path) noexcept : m_file_path(file_path) {}
    
    bool write() const noexcept;
    
    std::vector<std::string> modules{};
    std::vector<std::string> files{};
    std::vector<std::string> hooks{};

 private:
    std::string_view m_file_path{};
};

}  // namespace detail

#endif  // INITCPIO_HPP
