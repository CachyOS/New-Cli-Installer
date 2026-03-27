#ifndef LVM_HPP
#define LVM_HPP

#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for pair
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

// Show volume groups with their sizes
auto show_volume_groups() noexcept -> std::vector<std::pair<std::string, std::string>>;

/// @brief Parse vgs command output (tab-separated vg_name and vg_size)
/// @param output Raw output from: vgs -o vg_name,vg_size --noheading --separator='\t' --units=g
/// @return Vector of pairs (vg_name, vg_size)
auto parse_vgs_output(std::string_view output) noexcept -> std::vector<std::pair<std::string, std::string>>;

}  // namespace gucc::lvm

#endif  // LVM_HPP
