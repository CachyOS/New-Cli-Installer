#include "gucc/initcpio.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>  // for exists

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace {

constexpr auto modify_initcpio_line(std::string_view line, std::string_view modules, std::string_view files, std::string_view hooks) noexcept -> std::string {
    if (line.starts_with("MODULES"sv)) {
        return fmt::format(FMT_COMPILE("MODULES=({})"), modules);
    } else if (line.starts_with("FILES"sv)) {
        return fmt::format(FMT_COMPILE("FILES=({})"), files);
    } else if (line.starts_with("HOOKS"sv)) {
        return fmt::format(FMT_COMPILE("HOOKS=({})"), hooks);
    }
    return std::string{line.data(), line.size()};
}

constexpr auto modify_initcpio_fields(std::string_view file_content, std::string_view modules, std::string_view files, std::string_view hooks) noexcept -> std::string {
    return file_content | std::ranges::views::split('\n')
        | std::ranges::views::transform([&](auto&& rng) {
              auto&& line = std::string_view(&*rng.begin(), static_cast<size_t>(std::ranges::distance(rng)));
              return modify_initcpio_line(line, modules, files, hooks);
          })
        | std::ranges::views::join_with('\n')
        | std::ranges::to<std::string>();
}

}  // namespace

namespace gucc::detail {

bool Initcpio::write() const noexcept {
    if (!::fs::exists(m_file_path)) {
        spdlog::error("[INITCPIO] '{}' file doesn't exist!", m_file_path);
        return false;
    }

    auto&& file_content = file_utils::read_whole_file(m_file_path);
    if (file_content.empty()) {
        spdlog::error("[INITCPIO] '{}' error occurred!", m_file_path);
        return false;
    }
    const auto& formatted_modules = utils::join(modules, ' ');
    const auto& formatted_files   = utils::join(files, ' ');
    const auto& formatted_hooks   = utils::join(hooks, ' ');

    auto result = modify_initcpio_fields(file_content, formatted_modules, formatted_files, formatted_hooks);
    return file_utils::create_file_for_overwrite(m_file_path, result);
}

bool Initcpio::parse_file() noexcept {
    if (!::fs::exists(m_file_path)) {
        spdlog::error("[INITCPIO] '{}' file doesn't exist!", m_file_path);
        return false;
    }

    auto&& file_content = file_utils::read_whole_file(m_file_path);
    if (file_content.empty()) {
        spdlog::error("[INITCPIO] '{}' error occurred!", m_file_path);
        return false;
    }

    const auto& parse_line = [](std::string_view line) -> std::vector<std::string> {
        auto&& open_bracket_pos = line.find('(');
        auto&& close_bracket    = std::ranges::find(line, ')');
        if (open_bracket_pos != std::string_view::npos && close_bracket != line.end()) {
            const auto length = std::ranges::distance(line.begin() + static_cast<std::int64_t>(open_bracket_pos), close_bracket - 1);

            auto&& input_data = line.substr(open_bracket_pos + 1, static_cast<std::size_t>(length));
            return input_data | std::ranges::views::split(' ') | std::ranges::to<std::vector<std::string>>();
        }
        return {};
    };

    auto&& file_content_lines = utils::make_split_view(file_content);
    for (auto&& line : file_content_lines) {
        if (line.starts_with("MODULES"sv)) {
            modules = parse_line(std::move(line));
        } else if (line.starts_with("FILES"sv)) {
            files = parse_line(std::move(line));
        } else if (line.starts_with("HOOKS"sv)) {
            hooks = parse_line(std::move(line));
        }
    }

    return true;
}

}  // namespace gucc::detail

namespace gucc::initcpio {

auto setup_initcpio_config(std::string_view initcpio_path,
    const InitcpioConfig& config) noexcept -> bool {
    using gucc::fs::FilesystemType;

    auto initcpio = detail::Initcpio{initcpio_path};
    if (!initcpio.parse_file()) {
        return false;
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
            hooks = {"base", "systemd", "keyboard", "autodetect", "microcode", "kms", "modconf", "block", "sd-vconsole"};
        } else {
            hooks = {"base", "systemd", "autodetect", "microcode", "kms", "modconf", "block", "keyboard", "sd-vconsole"};
        }
    } else {
        if (config.is_luks) {
            hooks = {"base", "udev", "keyboard", "autodetect", "microcode", "kms", "modconf", "block", "keymap", "consolefont"};
        } else {
            hooks = {"base", "udev", "autodetect", "microcode", "kms", "modconf", "block", "keyboard", "keymap", "consolefont"};
        }
    }

    // plymouth comes before encrypt hooks
    if (config.has_plymouth) {
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
    initcpio.hooks = std::move(hooks);
    return initcpio.write();
}

}  // namespace gucc::initcpio
