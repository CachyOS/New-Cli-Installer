#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>        // for int32_t
#include <string>         // for string
#include <string_view>    // for string_view, hash
#include <unordered_map>  // for unordered_map
#include <variant>        // for variant
#include <vector>         // for vector

class Config final {
 public:
    using value_type      = std::unordered_map<std::string_view, std::variant<std::string, std::int32_t, std::vector<std::string>>>;
    using reference       = value_type&;
    using const_reference = const value_type&;

    Config() noexcept          = default;
    virtual ~Config() noexcept = default;

    static bool initialize() noexcept;
    static Config* instance();

    /* clang-format off */

    // Element access.
    auto data() noexcept -> reference
    { return m_data; }
    auto data() const noexcept -> const_reference
    { return m_data; }

    /* clang-format on */

 private:
    value_type m_data{};
};

#endif  // CONFIG_HPP
