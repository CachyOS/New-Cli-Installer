#include "gucc/locale.hpp"
#include "gucc/conf.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/keyboard.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for transform
#include <ranges>     // for ranges::*
#include <string>     // for string
#include <vector>     // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

auto list_localectl(std::string_view subcommand, std::string_view what) noexcept -> std::vector<std::string> {
    const auto& output = gucc::utils::exec(fmt::format(FMT_COMPILE("localectl {} 2>/dev/null"), subcommand));
    if (output.empty()) {
        spdlog::warn("Cannot list {}", what);
        return {};
    }
    return gucc::utils::make_multiline(output);
}

}  // namespace

namespace gucc::locale {

auto prepare_locale_set(std::string_view locale, std::string_view mountpoint) noexcept -> Result<void> {
    const auto& locale_config_path = utils::target_path(mountpoint, "/etc/locale.conf"sv);
    const auto& locale_gen_path    = utils::target_path(mountpoint, "/etc/locale.gen"sv);

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

    if (auto res = file_utils::write_file(locale_config_path, fmt::format(LOCALE_CONFIG_PART, locale)); !res) {
        return res;
    }

    // TODO(vnepogodin): refactor and make backups of locale config and locale gen
    const auto& locale_gen = file_utils::read_whole_file(locale_gen_path);
    if (locale_gen.empty() || !file_utils::write_file(locale_gen_path, detail::uncomment_locale(locale_gen, locale))) {
        spdlog::warn("Failed to uncomment locale '{}' in {}", locale, locale_gen_path);
    }

    // NOTE: maybe we should also write into /etc/default/locale if /etc/default exists and is a dir?
    return {};
}

auto set_locale(std::string_view locale, std::string_view mountpoint) noexcept -> Result<void> {
    // Prepare locale
    if (auto res = locale::prepare_locale_set(locale, mountpoint); !res) {
        return res;
    }

    // Generate locales
    return utils::arch_chroot_or_error("locale-gen"sv, mountpoint, "generate locales"sv);
}

auto parse_locale_gen(std::string_view content) noexcept -> std::vector<std::string> {
    return utils::make_split_view(content)
        | std::ranges::views::transform([](std::string_view line) { return conf::classify(line); })
        | std::ranges::views::join
        // `#  en_US.UTF-8 UTF-8` in the header is an example, not an entry
        | std::ranges::views::filter([](const conf::Line& line) { return !(line.commented && line.body.starts_with("  "sv)) && line.body.contains("UTF-8"sv); })
        | std::ranges::views::transform([](const conf::Line& line) { return std::string{utils::first_field(line.body)}; })
        | std::ranges::views::filter([](const auto& s) { return !s.empty(); })
        | std::ranges::to<std::vector<std::string>>();
}

auto get_possible_locales() noexcept -> std::vector<std::string> {
    return parse_locale_gen(file_utils::read_whole_file("/etc/locale.gen"sv));
}

auto set_xkbmap(std::string_view xkbmap, std::string_view mountpoint) noexcept -> Result<void> {
    // TODO(vnepogodin): should use localectl instead of doing manually
    keyboard::Config cfg{};
    cfg.x11_layout = xkbmap;
    return keyboard::apply(cfg, mountpoint);
}

auto get_x11_keymap_layouts() noexcept -> std::vector<std::string> {
    return list_localectl("list-x11-keymap-layouts"sv, "X11 keymap layouts"sv);
}

auto set_keymap(std::string_view keymap, std::string_view mountpoint) noexcept -> Result<void> {
    keyboard::Config cfg{};
    cfg.console_keymap = keymap;
    return keyboard::apply(cfg, mountpoint);
}

auto get_vconsole_keymap_list() noexcept -> std::vector<std::string> {
    return list_localectl("list-keymaps"sv, "console keymaps"sv);
}

}  // namespace gucc::locale

namespace gucc::locale::detail {

auto uncomment_locale(std::string_view content, std::string_view locale) noexcept -> std::string {
    return conf::uncomment_where(content, [locale](const conf::Line& line) {
        return !locale.empty() && utils::starts_with_word(line.body, locale, utils::kBlankChars);
    });
}

}  // namespace gucc::locale::detail
