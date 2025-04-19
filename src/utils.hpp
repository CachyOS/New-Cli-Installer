#ifndef UTILS_HPP
#define UTILS_HPP

#include "definitions.hpp"
#include "subprocess.h"

// import gucc
#include "gucc/partition.hpp"

#include <charconv>     // for from_chars
#include <functional>   // for function
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace utils {

[[nodiscard]] bool is_connected() noexcept;
bool prompt_char(const char* prompt, const char* color = RESET, char* read = nullptr) noexcept;
void clear_screen() noexcept;
void inst_needed(const std::string_view& pkg) noexcept;
void secure_wipe() noexcept;
void auto_partition() noexcept;
void generate_fstab() noexcept;
void set_hostname(const std::string_view& hostname) noexcept;
void set_locale(const std::string_view& locale) noexcept;
void set_xkbmap(const std::string_view& xkbmap) noexcept;
void set_keymap(const std::string_view& keymap) noexcept;
void set_timezone(const std::string_view& timezone) noexcept;
void set_hw_clock(const std::string_view& clock_type) noexcept;
void create_new_user(const std::string_view& user, const std::string_view& password, const std::string_view& shell) noexcept;
void set_root_password(const std::string_view& password) noexcept;
[[nodiscard]] bool check_mount() noexcept;
[[nodiscard]] bool check_base() noexcept;
[[nodiscard]] auto list_mounted() noexcept -> std::string;
[[nodiscard]] auto list_containing_crypt() noexcept -> std::string;
[[nodiscard]] auto list_non_crypt() noexcept -> std::string;
void lvm_detect(std::optional<std::function<void()>> func_callback = std::nullopt) noexcept;
void umount_partitions() noexcept;
void find_partitions() noexcept;

[[nodiscard]] auto get_pkglist_base(const std::string_view& packages) noexcept -> std::optional<std::vector<std::string>>;
[[nodiscard]] auto get_pkglist_desktop(const std::string_view& desktop) noexcept -> std::optional<std::vector<std::string>>;
auto install_from_pkglist(const std::string_view& packages) noexcept -> bool;
void install_base(const std::string_view& packages) noexcept;
void install_desktop(const std::string_view& desktop) noexcept;
void remove_pkgs(const std::string_view& packages) noexcept;
void install_grub_uefi(const std::string_view& bootid, bool as_default = true) noexcept;
void install_refind() noexcept;
void install_systemd_boot() noexcept;
void uefi_bootloader(const std::string_view& bootloader) noexcept;
void bios_bootloader(const std::string_view& bootloader) noexcept;
void install_bootloader(const std::string_view& bootloader) noexcept;

void get_cryptroot() noexcept;
void get_cryptboot() noexcept;
void boot_encrypted_setting() noexcept;
void recheck_luks() noexcept;

void arch_chroot(const std::string_view& command, bool follow = true) noexcept;
auto exec_follow(const std::vector<std::string>& vec, std::string& process_log, bool& running, subprocess_s& child, bool async = true) noexcept -> bool;
void dump_to_log(const std::string& data) noexcept;
void dump_settings_to_log() noexcept;
void dump_partition_to_log(const gucc::fs::Partition& partition) noexcept;
void dump_partitions_to_log(const std::vector<gucc::fs::Partition>& partitions) noexcept;
[[nodiscard]] bool check_root() noexcept;
void id_system() noexcept;
[[nodiscard]] bool handle_connection() noexcept;
void show_iwctl() noexcept;

void set_keymap() noexcept;
void enable_autologin(const std::string_view& dm, const std::string_view& username) noexcept;
bool parse_config() noexcept;
void setup_luks_keyfile() noexcept;
void grub_mkconfig() noexcept;
void enable_services() noexcept;
void final_check() noexcept;

template <typename T = std::int32_t>
    requires std::numeric_limits<T>::is_integer
inline T to_int(const std::string_view& str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}

template <typename T = double>
    requires std::is_floating_point_v<T>
inline T to_floating(const std::string_view& str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}

// convert number, unit to bytes
constexpr inline double convert_unit(const double number, const std::string_view& unit) {
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

// Get appropriate default mountpoint for bootloader
constexpr inline auto bootloader_default_mount(std::string_view bootloader, std::string_view bios_mode) noexcept -> std::string_view {
    using namespace std::string_view_literals;

    if (bootloader == "systemd-boot"sv || bios_mode == "BIOS"sv) {
        return "/boot"sv;
    } else if (bootloader == "grub"sv || bootloader == "refind"sv) {
        return "/boot/efi"sv;
    }
    return "unknown bootloader"sv;
}

// Get available bootloaders
constexpr inline auto available_bootloaders(std::string_view bios_mode) noexcept -> std::vector<std::string_view> {
    using namespace std::string_view_literals;

    if (bios_mode == "BIOS"sv) {
        return {"grub"sv};
    }
    return {"systemd-boot"sv, "refind"sv, "grub"sv};
}

}  // namespace utils

#endif  // UTILS_HPP
