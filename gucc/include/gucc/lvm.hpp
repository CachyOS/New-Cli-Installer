#ifndef LVM_HPP
#define LVM_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace gucc::lvm {

// LVM detection result
struct LvmInfo {
    std::vector<std::string> physical_volumes{};
    std::vector<std::string> volume_groups{};
    std::vector<std::string> logical_volumes{};

    [[nodiscard]] constexpr bool is_active() const noexcept {
        return !physical_volumes.empty() && !volume_groups.empty() && !logical_volumes.empty();
    }
};

// Detect existing LVM setup
auto detect_lvm() noexcept -> LvmInfo;

// Activate LVM volumes (load dm-mod, vgscan, vgchange)
auto activate_lvm() noexcept -> bool;

}  // namespace gucc::lvm

#endif  // LVM_HPP
