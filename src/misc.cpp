#include "misc.hpp"
#include "installer_state.hpp"
#include "utils.hpp"
#include "widgets.hpp"

// import gucc
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/mtab.hpp"
#include "gucc/string_utils.hpp"

/* clang-format off */
#include <filesystem>                              // for exists, is_directory
#include <functional>                              // for function
#include <ranges>                                  // for ranges::*
#include <string>                                  // for string
#include <vector>                                  // for vector
/* clang-format on */

#include <fmt/compile.h>
#include <fmt/core.h>

using namespace std::string_literals;
using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace {

void edit_file(std::string_view path) noexcept {
    tui::detail::run_in_terminal({"/usr/bin/vim"s, std::string{path}});
}

void view_log(std::string_view content) noexcept {
    tui::detail::run_in_terminal({"/usr/bin/fzf"s, "--reverse"s, "--header=Exit by pressing esc"s, "--prompt=Type to filter log entries > "s}, content);
}

void edit_mkinitcpio() noexcept {
#ifdef NDEVENV
    const auto mountpoint = utils::get_mountpoint();
#else
    const auto mountpoint = "/"sv;
#endif

    edit_file(gucc::utils::target_path(mountpoint, "/etc/mkinitcpio.conf"sv));
    if (tui::detail::yesno_widget("Run mkinitcpio?"sv)) {
        utils::arch_chroot({"/usr/bin/mkinitcpio"s, "-P"s});
    }
}

void edit_grub() noexcept {
#ifdef NDEVENV
    const auto mountpoint = utils::get_mountpoint();
#else
    const auto mountpoint = "/"sv;
#endif

    edit_file(gucc::utils::target_path(mountpoint, "/etc/default/grub"sv));
    if (tui::detail::yesno_widget("Update GRUB?"sv)) {
        utils::grub_mkconfig();
    }
}
}  // namespace

namespace tui {

// Revised to deal with partition sizes now being displayed to the user
bool confirm_mount([[maybe_unused]] std::string_view part_user, bool quite) {
#ifdef NDEVENV
    const auto& mounts    = gucc::mtab::parse_mtab(part_user);
    const bool is_mounted = mounts && !mounts->empty();
    if (!quite && !is_mounted) {
        detail::infobox_for("Mount Failed!"sv, std::chrono::seconds(2));
        return false;
    }
#endif
    if (!quite) {
        detail::infobox_for("Mount Successful!"sv, std::chrono::seconds(2));
    }
    utils::remove_from_partition_list(tui::state().disk.partition);
    return true;
}

// Choose pacman cache
void set_cache() noexcept {
    static constexpr auto content     = "Do you want to use the pacman cache of the running system instead of the installation target? This can reduce the size of the required downloads in the installation."sv;
    tui::state().inputs.ctx.hostcache = detail::yesno_widget(content);
}

void edit_configs() noexcept {
    const auto& uefi_mount = tui::state().inputs.ctx.uefi_mount;
#ifdef NDEVENV
    const auto mountpoint = utils::get_mountpoint();
#else
    const auto mountpoint = "/"sv;
#endif

    std::vector<detail::MenuEntry> entries{};
    const auto add_entry = [&entries](const std::string& path, std::string label, std::function<void()> edit) {
        if (!fs::exists(path)) {
            return;
        }
        entries.push_back({std::move(label), [edit = std::move(edit)] { edit(); return true; }});
    };
    const auto add_file = [&add_entry](const std::string& path, std::string_view owner = {}) {
        auto name  = fs::path{path}.filename().string();
        auto label = owner.empty() ? std::move(name) : fmt::format(FMT_COMPILE("{} {}"), name, owner);
        add_entry(path, std::move(label), [path] { edit_file(path); });
    };

    if (const auto& home = gucc::utils::target_path(mountpoint, "/home"sv); fs::exists(home)) {
        for (auto&& dir_entry : fs::directory_iterator{home}) {
            const auto& user_dir = dir_entry.path();
            for (const auto* name : {".extend.xinitrc", ".xinitrc", ".extend.Xresources", ".Xresources"}) {
                add_file((user_dir / name).string(), user_dir.filename().string());
            }
        }
    }

    const std::vector<std::string> plain_files{
        "/etc/crypttab"s,
        "/etc/fstab"s,
        "/boot/refind_linux.conf"s,
        fmt::format(FMT_COMPILE("{}/EFI/refind/refind_linux.conf"), uefi_mount),
        fmt::format(FMT_COMPILE("{}/loader/loader.conf"), uefi_mount),
        "/etc/hostname"s,
        "/etc/hosts"s,
        "/etc/systemd/journald.conf"s,
        "/etc/conf.d/keymaps"s,
        "/etc/locale.conf"s,
        "/etc/lightdm/lightdm.conf"s,
        "/etc/lxdm/lxdm.conf"s,
        "/etc/rc.conf"s,
        "/etc/pacman.conf"s,
        "/etc/makepkg.conf"s,
        "/etc/plasmalogin.conf"s,
        "/etc/sddm.conf"s,
        "/etc/sudoers.conf"s,
        "/boot/syslinux/syslinux.cfg"s,
        "/etc/vconsole.conf"s,
    };
    for (const auto& file : plain_files) {
        add_file(gucc::utils::target_path(mountpoint, file));
    }

    // run additional configuration action once edited
    add_entry(gucc::utils::target_path(mountpoint, "/etc/default/grub"sv), "grub"s, edit_grub);
    add_entry(gucc::utils::target_path(mountpoint, "/etc/mkinitcpio.conf"sv), "mkinitcpio.conf"s, edit_mkinitcpio);

    static constexpr auto seeconf_body = "Select any file listed below to be reviewed or amended."sv;
    detail::menu_loop(seeconf_body, entries);
}

void edit_pacman_conf() noexcept {
    edit_file("/etc/pacman.conf"sv);

#ifdef NDEVENV
    // NOTE: don't care now, Will change in future..
    detail::infobox_widget("Updating database ..."sv);
    detail::run_in_terminal({"/usr/bin/pacman"s, "-Syy"s});
#endif
}

void logs_menu() noexcept {
#ifdef NDEVENV
    const auto mountpoint = utils::get_mountpoint();
#else
    const auto mountpoint = "/"sv;
#endif

    struct LogSource {
        std::string_view label;
        std::function<std::string()> read;
    };
    const std::vector<LogSource> logs{
        {"Dmesg"sv, [mountpoint] { return gucc::utils::arch_chroot_query(std::vector{"/usr/bin/dmesg"s}, mountpoint); }},
        {"Pacman log"sv, [mountpoint] { return gucc::file_utils::read_whole_file(gucc::utils::target_path(mountpoint, "/var/log/pacman.log"sv)); }},
        {"Xorg log"sv, [mountpoint] { return gucc::file_utils::read_whole_file(gucc::utils::target_path(mountpoint, "/var/log/Xorg.0.log"sv)); }},
        {"Journalctl"sv, [mountpoint] { return gucc::utils::arch_chroot_query(std::vector{"/usr/bin/journalctl"s}, mountpoint); }},
    };

    auto entries = logs
        | std::ranges::views::transform([](const LogSource& log) {
              return detail::MenuEntry{std::string{log.label}, [&log] { view_log(log.read()); return true; }};
          })
        | std::ranges::to<std::vector<detail::MenuEntry>>();
    entries.push_back({"Back"s, detail::kLeaveMenu});
    detail::menu_loop({}, entries);
}

}  // namespace tui
