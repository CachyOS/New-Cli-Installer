#include "gucc/initcpio.hpp"
#include "detail/initcpio_impl.hpp"
#include "gucc/conf.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"

#include <array>        // for array
#include <filesystem>   // for exists
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace {

using gucc::detail::InitcpioArrays;

// same calamares hooks and paths.
constexpr auto kDeferralHookPath   = "/etc/pacman.d/hooks/90-mkinitcpio-install.hook"sv;
constexpr auto kDeferralScriptPath = "/usr/share/libalpm/scripts/mkinitcpio-install-calamares"sv;

constexpr auto kDeferralHook = R"([Trigger]
Type = Path
Operation = Install
Operation = Upgrade
Target = usr/lib/modules/*/vmlinuz
Target = usr/lib/initcpio/*

[Action]
Description = Updating linux initcpios...
When = PostTransaction
Exec = /usr/share/libalpm/scripts/mkinitcpio-install-calamares
NeedsTargets
)"sv;

// copy of mkinitcpio-install-calamares
constexpr auto kDeferralScript = R"(#!/bin/bash -e

args=()
all=0

while read -r line; do
    if [[ $line != */vmlinuz ]]; then
        # triggers when it's a change to usr/lib/initcpio/*
        all=1
        continue
    fi

    if ! read -r pkgbase > /dev/null 2>&1 < "${line%/vmlinuz}/pkgbase"; then
        # if the kernel has no pkgbase, we skip it
        continue
    fi

    preset="/etc/mkinitcpio.d/${pkgbase}.preset"
    if [[ ! -e $preset ]]; then
        if [[ -e $preset.pacsave ]]; then
            # move the pacsave to the template
            mv "${preset}.pacsave" "$preset"
        else
            # create the preset from the template
            sed "s|%PKGBASE%|${pkgbase}|g" /usr/share/mkinitcpio/hook.preset \
                | install -Dm644 /dev/stdin "$preset"
        fi
    fi

    # always install the kernel
    install -Dm644 "${line}" "/boot/vmlinuz-${pkgbase}"

    # compound args for each kernel
    args+=(-p "${pkgbase}")
done

if (( all )) && compgen -G /etc/mkinitcpio.d/"*.preset" > /dev/null; then
    # change to use all presets
    args=(-P)
fi
)"sv;

struct Joined {
    std::string_view modules;
    std::string_view files;
    std::string_view hooks;
};

struct Key {
    std::string_view Joined::* joined{};
    std::string_view name;
    std::vector<std::string> InitcpioArrays::* field{};
};

// map every field
static constexpr std::array KEYS{
    Key{.joined = &Joined::modules, .name = "MODULES"sv, .field = &InitcpioArrays::modules},
    Key{.joined = &Joined::files, .name = "FILES"sv, .field = &InitcpioArrays::files},
    Key{.joined = &Joined::hooks, .name = "HOOKS"sv, .field = &InitcpioArrays::hooks},
};

struct Assignment {
    Key key;
    std::string_view value;
};

constexpr auto match_key(std::string_view line) noexcept -> std::optional<Assignment> {
    return gucc::conf::active(line).and_then([](std::string_view body) { return gucc::utils::split_once(body, '='); }).and_then([](auto assignment) {
        return gucc::utils::find_optional(KEYS, assignment.first, &Key::name).transform([&assignment](const Key& key) {
            return Assignment{.key = key, .value = assignment.second};
        });
    });
}

constexpr auto words(std::string_view line) noexcept {
    return gucc::utils::words(line, gucc::utils::kBlankChars)
        | std::ranges::views::take_while([](std::string_view word) { return !word.starts_with('#'); });
}

auto parse_array(std::string_view value) -> std::vector<std::string> {
    const auto open = value.find('(');
    if (open == std::string_view::npos) {
        return {};
    }
    std::vector<std::string> values{};
    for (const auto word : words(value.substr(open + 1))) {
        const auto close = word.find(')');
        if (const auto head = word.substr(0, close); !head.empty()) {
            values.emplace_back(gucc::utils::unquote(head));
        }
        if (close != std::string_view::npos) {
            return values;
        }
    }
    return {};
}

// maybe also move to public io util ?
auto write_target_file(std::string_view root, std::string_view path, std::string_view content) noexcept -> gucc::Result<void> {
    const auto target = gucc::utils::target_path(root, path);
    if (auto res = gucc::file_utils::create_directories(fs::path{target}.parent_path().string()); !res) {
        return res;
    }
    return gucc::file_utils::write_file(target, content);
}

}  // namespace

namespace gucc::detail {

auto parse_initcpio(std::string_view content) noexcept -> InitcpioArrays {
    InitcpioArrays arrays{};
    for (const auto line : utils::split_lines(content)) {
        if (const auto assignment = match_key(line)) {
            arrays.*assignment->key.field = parse_array(assignment->value);
        }
    }
    return arrays;
}

auto modify_initcpio_line(std::string_view line, std::string_view modules, std::string_view files, std::string_view hooks) noexcept -> std::string {
    const Joined joined{.modules = modules, .files = files, .hooks = hooks};
    return match_key(line)
        .transform([&joined](const Assignment& assignment) { return fmt::format(FMT_COMPILE("{}=({})"), assignment.key.name, joined.*assignment.key.joined); })
        .value_or(std::string{line});
}

auto rewrite_initcpio(std::string_view content, std::string_view modules, std::string_view files, std::string_view hooks) noexcept -> std::string {
    return utils::map_lines(content, [&](std::string_view line) { return modify_initcpio_line(line, modules, files, hooks); });
}

auto Initcpio::read_config() const noexcept -> std::optional<std::string> {
    if (!::fs::exists(m_file_path)) {
        spdlog::error("[INITCPIO] '{}' file doesn't exist!", m_file_path);
        return std::nullopt;
    }
    auto file_content = file_utils::read_whole_file(m_file_path);
    if (file_content.empty()) {
        spdlog::error("[INITCPIO] '{}' error occurred!", m_file_path);
        return std::nullopt;
    }
    return file_content;
}

bool Initcpio::write() const noexcept {
    const auto& file_content = read_config();
    if (!file_content) {
        return false;
    }
    const auto& formatted_modules = utils::join(m_arrays.modules, ' ');
    const auto& formatted_files   = utils::join(m_arrays.files, ' ');
    const auto& formatted_hooks   = utils::join(m_arrays.hooks, ' ');

    return file_utils::create_file_for_overwrite(m_file_path, rewrite_initcpio(*file_content, formatted_modules, formatted_files, formatted_hooks));
}

bool Initcpio::parse_file() noexcept {
    const auto& file_content = read_config();
    if (!file_content) {
        return false;
    }

    m_arrays = parse_initcpio(*file_content);
    return true;
}

}  // namespace gucc::detail

namespace gucc::initcpio {

auto setup_initcpio_config(std::string_view initcpio_path,
    const InitcpioConfig& config) noexcept -> Result<void> {
    using gucc::fs::FilesystemType;

    auto initcpio = detail::Initcpio{initcpio_path};
    if (!initcpio.parse_file()) {
        return make_error(ErrorCode::ParseError, FMT_COMPILE("initcpio: failed to parse config '{}'"), initcpio_path);
    }

    // don't read previous/existing hooks, and instead overwrite with the ones we expect
    std::vector<std::string> hooks;

    // ZFS and bcachefs disable systemd hooks
    const bool systemd_allowed = config.use_systemd_hook
        && config.filesystem_type != FilesystemType::Zfs;

    // keyboard should come before autodetect
    // so the keyboard is available for passphrase entry
    // NOTE: For systems that are booted with different hardware configurations
    // (e.g. laptops with external keyboard vs. internal keyboard or headless
    // systems), this hook needs to be placed before autodetect in order to be
    // able to use the keyboard at boot time, for example to unlock an
    // encrypted device when using the encrypt hook.
    if (systemd_allowed) {
        if (config.is_luks) {
            hooks = {"base", "systemd", "keyboard", "autodetect", "microcode", "modconf", "block", "sd-vconsole"};
        } else {
            hooks = {"base", "systemd", "autodetect", "microcode", "modconf", "block", "keyboard", "sd-vconsole"};
        }
    } else {
        if (config.is_luks) {
            hooks = {"base", "udev", "keyboard", "autodetect", "microcode", "modconf", "block", "keymap", "consolefont"};
        } else {
            hooks = {"base", "udev", "autodetect", "microcode", "modconf", "block", "keyboard", "keymap", "consolefont"};
        }
    }

    // drop plymouth zfs encryption or if not installed
    const bool zfs_encrypted = (config.filesystem_type == FilesystemType::Zfs)
        && (config.is_zfs_encrypted || config.is_luks);
    if (config.has_plymouth && !zfs_encrypted) {
        hooks.emplace_back("plymouth");
    }

    // encryption related
    if (config.is_luks) {
        if (systemd_allowed) {
            hooks.emplace_back("sd-encrypt");
        } else {
            hooks.emplace_back("encrypt");
        }
    }

    // LVM2 hook is necessary for root file system on LVM
    if (config.is_lvm) {
        hooks.emplace_back("lvm2");
    }

    // ZFS specific
    if (config.filesystem_type == FilesystemType::Zfs) {
        hooks.emplace_back("zfs");
    }

    // btrfs hook for multi-device setups
    if (config.filesystem_type == FilesystemType::Btrfs && config.is_btrfs_multi_device) {
        hooks.emplace_back("btrfs");
    }

    // Resume/hibernation is required only for busybox
    // NOTE: When the default systemd-based initramfs (using the systemd hook)
    // is used, a resume mechanism is already provided and no further hooks are
    // required.
    if (config.has_swap && !systemd_allowed) {
        hooks.emplace_back("resume");
    }
    hooks.emplace_back("filesystems");

    // fsck not needed for btrfs and zfs
    if (config.filesystem_type != FilesystemType::Btrfs && config.filesystem_type != FilesystemType::Zfs) {
        hooks.emplace_back("fsck");
    }

    // write/flush to disk
    initcpio.arrays().hooks = std::move(hooks);
    if (!initcpio.write()) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("initcpio: failed to write config '{}'"), initcpio_path);
    }
    return {};
}

auto install_deferral_hook(std::string_view root) noexcept -> Result<void> {
    if (auto res = write_target_file(root, kDeferralScriptPath, kDeferralScript); !res) {
        return res;
    }
    std::error_code ec{};
    ::fs::permissions(utils::target_path(root, kDeferralScriptPath), ::fs::perms::owner_exec | ::fs::perms::group_exec | ::fs::perms::others_exec, ::fs::perm_options::add, ec);
    if (ec) {
        return make_error(ErrorCode::FileIo, FMT_COMPILE("initcpio: cannot make '{}' executable: {}"), kDeferralScriptPath, ec.message());
    }
    return write_target_file(root, kDeferralHookPath, kDeferralHook);
}

auto remove_deferral_hook(std::string_view root) noexcept -> Result<void> {
    for (const auto path : {kDeferralHookPath, kDeferralScriptPath}) {
        std::error_code ec{};
        ::fs::remove(utils::target_path(root, path), ec);
        if (ec) {
            return make_error(ErrorCode::FileIo, FMT_COMPILE("initcpio: cannot remove '{}': {}"), path, ec.message());
        }
    }
    return {};
}

}  // namespace gucc::initcpio
