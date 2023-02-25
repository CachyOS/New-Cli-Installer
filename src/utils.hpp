#ifndef UTILS_HPP
#define UTILS_HPP

#include "definitions.hpp"
#include "subprocess.h"

#include <charconv>     // for from_chars
#include <functional>   // for function
#include <optional>     // for optional
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>

namespace utils {

[[nodiscard]] bool is_connected() noexcept;
bool prompt_char(const char* prompt, const char* color = RESET, char* read = nullptr) noexcept;
void clear_screen() noexcept;
[[nodiscard]] auto make_multiline(const std::string_view& str, bool reverse = false, char delim = '\n') noexcept -> std::vector<std::string>;
[[nodiscard]] auto make_multiline(const std::vector<std::string>& multiline, bool reverse = false, const std::string_view&& delim = "\n") noexcept -> std::string;
void inst_needed(const std::string_view& pkg) noexcept;
void secure_wipe() noexcept;
void auto_partition() noexcept;
void generate_fstab(const std::string_view& fstab_cmd) noexcept;
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
[[nodiscard]] auto get_mountpoint_fs(const std::string_view& mountpoint) noexcept -> std::string;
[[nodiscard]] auto get_mountpoint_source(const std::string_view& mountpoint) noexcept -> std::string;
[[nodiscard]] auto list_containing_crypt() noexcept -> std::string;
[[nodiscard]] auto list_non_crypt() noexcept -> std::string;
void lvm_detect(std::optional<std::function<void()>> func_callback = std::nullopt) noexcept;
void umount_partitions() noexcept;
void find_partitions() noexcept;

[[nodiscard]] auto get_pkglist_base(const std::string_view& packages) noexcept -> std::vector<std::string>;
[[nodiscard]] auto get_pkglist_desktop(const std::string_view& desktop) noexcept -> std::vector<std::string>;
void install_from_pkglist(const std::string_view& packages) noexcept;
void install_base(const std::string_view& kernel) noexcept;
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
void exec_follow(const std::vector<std::string>& vec, std::string& process_log, bool& running, subprocess_s& child, bool async = true) noexcept;
void exec(const std::vector<std::string>& vec) noexcept;
auto exec(const std::string_view& command, const bool& interactive = false) noexcept -> std::string;
[[nodiscard]] auto read_whole_file(const std::string_view& filepath) noexcept -> std::string;
bool write_to_file(const std::string_view& data, const std::string_view& filepath) noexcept;
void dump_to_log(const std::string& data) noexcept;
[[nodiscard]] bool check_root() noexcept;
void id_system() noexcept;
[[nodiscard]] bool handle_connection() noexcept;
void show_iwctl() noexcept;

void set_keymap() noexcept;
void enable_autologin(const std::string_view& dm, const std::string_view& user) noexcept;
void set_schedulers() noexcept;
void set_swappiness() noexcept;
bool parse_config() noexcept;
void setup_luks_keyfile() noexcept;
void grub_mkconfig() noexcept;
void enable_services() noexcept;
void final_check() noexcept;

template <typename T = std::int32_t,
    typename         = std::enable_if_t<std::numeric_limits<T>::is_integer>>
inline T to_int(const std::string_view& str) {
    T result = 0;
    std::from_chars(str.data(), str.data() + str.size(), result);
    return result;
}

template <typename T = double,
    typename         = std::enable_if_t<std::is_floating_point<T>::value>>
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

}  // namespace utils

#endif  // UTILS_HPP
