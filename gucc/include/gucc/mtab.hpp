#ifndef MTAB_HPP
#define MTAB_HPP

#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::mtab {

struct MTabEntry {
    std::string device{};
    std::string mountpoint{};
};

// Parse mtab
auto parse_mtab(std::string_view root_mountpoint) noexcept -> std::optional<std::vector<MTabEntry>>;

// Parse mtab content
auto parse_mtab_content(std::string_view mtab_content, std::string_view root_mountpoint) noexcept -> std::vector<MTabEntry>;

}  // namespace gucc::mtab

#endif  // MTAB_HPP
