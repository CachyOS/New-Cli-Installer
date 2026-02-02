#include "gucc/locale.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>   // for transform
#include <filesystem>  // for path
#include <ranges>      // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::locale {

auto prepare_locale_set(std::string_view locale, std::string_view mountpoint) noexcept -> bool {
    const auto& locale_config_path = fmt::format(FMT_COMPILE("{}/etc/locale.conf"), mountpoint);
    const auto& locale_gen_path    = fmt::format(FMT_COMPILE("{}/etc/locale.gen"), mountpoint);

    static constexpr auto LOCALE_CONFIG_PART = R"(LANG="{0}"
LC_NUMERIC="{0}"
LC_TIME="{0}"
LC_MONETARY="{0}"
LC_PAPER="{0}"
LC_NAME="{0}"
LC_ADDRESS="{0}"
LC_TELEPHONE="{0}"
LC_MEASUREMENT="{0}"
LC_IDENTIFICATION="{0}"
LC_MESSAGES="{0}"
)";

    {
        const auto& locale_config_text = fmt::format(LOCALE_CONFIG_PART, locale);
        if (!file_utils::create_file_for_overwrite(locale_config_path, locale_config_text)) {
            spdlog::error("Failed to open locale config for writing {}", locale_config_path);
            return false;
        }
    }

    // TODO(vnepogodin): refactor and make backups of locale config and locale gen
    utils::exec(fmt::format(FMT_COMPILE("sed -i \"s/#{0}/{0}/\" {1}"), locale, locale_gen_path));

    // NOTE: maybe we should also write into /etc/default/locale if /etc/default exists and is a dir?
    return true;
}

auto set_locale(std::string_view locale, std::string_view mountpoint) noexcept -> bool {
    // Prepare locale
    if (!locale::prepare_locale_set(locale, mountpoint)) {
        spdlog::error("Failed to prepare locale files for '{}'", locale);
        return false;
    }

    // Generate locales
    if (!utils::arch_chroot_checked("locale-gen", mountpoint)) {
        spdlog::error("Failed to run locale-gen with locale '{}'", locale);
        return false;
    }

    return true;
}

auto get_possible_locales() noexcept -> std::vector<std::string> {
    const auto& locales = utils::exec("cat /etc/locale.gen | grep -v '#  ' | sed 's/#//g' | awk '/UTF-8/ {print $1}'");
    return utils::make_multiline(locales);
}

auto set_xkbmap(std::string_view xkbmap, std::string_view mountpoint) noexcept -> bool {
    // TODO(vnepogodin): should use localectl instead of doing manually
    static constexpr auto XKBMAP_CONFIG = R"(Section "InputClass"
    Identifier "system-keyboard"
    MatchIsKeyboard "on"
    Option "XkbLayout" "{}"
EndSection
)";

    const auto& xorg_conf_dir      = fmt::format(FMT_COMPILE("{}/etc/X11/xorg.conf.d"), mountpoint);
    const auto& keyboard_conf_path = fmt::format(FMT_COMPILE("{}/00-keyboard.conf"), xorg_conf_dir);

    // Create directory if it doesn't exist
    std::error_code ec{};
    std::filesystem::create_directories(xorg_conf_dir, ec);
    if (ec) {
        spdlog::error("Failed to create xorg.conf.d directory: {}", ec.message());
        return false;
    }

    const auto& config_content = fmt::format(XKBMAP_CONFIG, xkbmap);
    if (!file_utils::create_file_for_overwrite(keyboard_conf_path, config_content)) {
        spdlog::error("Failed to write keyboard config to {}", keyboard_conf_path);
        return false;
    }

    spdlog::info("X11 keyboard layout set to '{}'", xkbmap);
    return true;
}

auto get_x11_keymap_layouts() noexcept -> std::vector<std::string> {
    const auto& layouts = utils::exec("localectl list-x11-keymap-layouts 2>/dev/null");
    if (layouts.empty()) {
        spdlog::warn("Cannot list X11 keymap layouts");
        return {};
    }
    return utils::make_multiline(layouts);
}

}  // namespace gucc::locale
