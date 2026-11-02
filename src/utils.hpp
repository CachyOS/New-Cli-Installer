#pragma once

#include "definitions.hpp"

#include "cachyos/installer_config.hpp"

// import gucc
#include "gucc/bootloader.hpp"
#include "gucc/partition.hpp"

#include <cstdint>  // for uint64_t

#include <charconv>     // for from_chars
#include <expected>     // for expected
#include <functional>   // for function
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

// forward-declare
namespace cachyos::installer {
struct InstallContext;
}  // namespace cachyos::installer

namespace utils::detail {

[[nodiscard]] auto kernel_from_initramfs_name(std::string_view filename) noexcept -> std::optional<std::string>;

}  // namespace utils::detail

namespace utils {

[[nodiscard]] auto build_install_context() noexcept -> cachyos::installer::InstallContext;
[[nodiscard]] auto get_mountpoint() noexcept -> std::string_view;
[[nodiscard]] bool is_connected() noexcept;
bool prompt_char(const char* prompt, const char* color = RESET, char* read = nullptr) noexcept;
void clear_screen() noexcept;
void secure_wipe() noexcept;
auto auto_partition() noexcept -> std::vector<gucc::fs::Partition>;
void create_new_user(std::string_view user, std::string_view password, std::string_view shell) noexcept;
[[nodiscard]] bool check_mount() noexcept;
[[nodiscard]] bool check_base() noexcept;
[[nodiscard]] auto device_listing(std::string_view columns, std::string_view filter, std::string_view device = {}) noexcept -> std::string;
[[nodiscard]] auto list_mounted() noexcept -> std::string;
[[nodiscard]] auto list_containing_crypt() noexcept -> std::string;
[[nodiscard]] auto list_non_crypt() noexcept -> std::string;
void lvm_detect(std::function<void()> on_active = {}) noexcept;
void umount_partitions() noexcept;

/// Partition list filter.
enum class PartitionPurpose : std::uint8_t {
    Mount,     // part, lvm, crypt, root, ESP on UEFI
    OpenLuks,  // part, lvm, crypt
    LvmPv,     // part, crypt
    Encrypt,   // part, lvm
};
void find_partitions(PartitionPurpose purpose = PartitionPurpose::Mount) noexcept;

/// Activates LVM, unmounts the target,
/// remounts ZFS datasets and re-lists the partitions for mounting.
void refresh_partition_list(std::function<void()> on_lvm_active = {}) noexcept;

/// Drops device from the partition menu list once it has been taken.
/// NOTE: legacy
void remove_from_partition_list(std::string_view device) noexcept;

auto install_from_pkglist(std::string_view packages) noexcept -> bool;
void install_base(std::string_view packages) noexcept;
void install_desktop(std::string_view desktop) noexcept;
void remove_pkgs(std::string_view packages) noexcept;
auto setup_esp_partition(std::string_view device, std::string_view mountpoint, bool format_requested) noexcept -> std::optional<gucc::fs::Partition>;
void install_bootloader(gucc::bootloader::BootloaderType bootloader) noexcept;

// TODO(vnepogodin): shouldn't that hardcoded and poorly handheld. let's use something structured and firm (gucc::fs::Partition for example)
auto get_cryptroot() noexcept -> bool;
auto get_cryptboot() noexcept -> bool;
auto boot_encrypted_setting() noexcept -> bool;
auto recheck_luks() noexcept -> bool;

void arch_chroot(const std::vector<std::string>& argv) noexcept;
void dump_partition_to_log(const gucc::fs::Partition& partition) noexcept;
void dump_partitions_to_log(const std::vector<gucc::fs::Partition>& partitions) noexcept;
[[nodiscard]] bool check_root() noexcept;
void id_system() noexcept;
[[nodiscard]] bool handle_connection() noexcept;
void show_iwctl() noexcept;

void enable_autologin(std::string_view dm, std::string_view username) noexcept;
[[nodiscard]] auto parse_config(std::string_view config_path = "settings.json") noexcept -> std::expected<cachyos::installer::InstallerConfig, std::string>;
auto setup_luks_keyfile() noexcept -> bool;
void grub_mkconfig() noexcept;

[[nodiscard]] auto list_installed_kernels(std::string_view boot_dir) noexcept -> std::vector<std::string>;
[[nodiscard]] auto enabled_display_manager(std::string_view root_mountpoint) noexcept -> std::string;
[[nodiscard]] auto list_home_users(std::string_view root_mountpoint) noexcept -> std::vector<std::string>;

template <typename T = std::int32_t>
    requires std::numeric_limits<T>::is_integer
inline T to_int(std::string_view str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}

template <typename T = double>
    requires std::is_floating_point_v<T>
inline T to_floating(std::string_view str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}

// convert number, unit to bytes
constexpr inline double convert_unit(const double number, std::string_view unit) {
    if (unit == "KB" || unit == "K") {  // assuming KiB not KB
        return number * 1024;
    } else if (unit == "MB" || unit == "M") {
        return number * 1024 * 1024;
    } else if (unit == "GB" || unit == "G") {
        return number * 1024 * 1024 * 1024;
    }
    // for "bytes"
    return number;
}

/// @brief Replace all occurrences of a substring in a string.
/// @param inout The string to modify.
/// @param what The substring to replace.
/// @param with The replacement string.
/// @return The number of replacements made.
inline std::size_t replace_all(std::string& inout, std::string_view what, std::string_view with) noexcept {
    std::size_t count{};
    std::size_t pos{};
    while (std::string::npos != (pos = inout.find(what.data(), pos, what.length()))) {
        inout.replace(pos, what.length(), with.data(), with.length());
        pos += with.length(), ++count;
    }
    return count;
}

/// @brief Remove all occurrences of a substring from a string.
/// @param inout The string to modify.
/// @param what The substring to remove.
/// @return The number of removals made.
inline std::size_t remove_all(std::string& inout, std::string_view what) noexcept {
    return replace_all(inout, what, "");
}

}  // namespace utils
