#include "gucc/keyboard.hpp"
#include "gucc/file_utils.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {
auto as_view(const std::optional<std::string>& opt) noexcept -> std::optional<std::string_view> {
    if (!opt) {
        return std::nullopt;
    }
    return std::string_view{*opt};
}
}  // namespace

namespace gucc::keyboard {

auto make_vconsole_conf(std::string_view keymap, std::optional<std::string_view> font) noexcept -> std::string {
    auto out = fmt::format(FMT_COMPILE("KEYMAP={}\n"), keymap);
    if (font) {
        out += fmt::format(FMT_COMPILE("FONT={}\n"), *font);
    }
    return out;
}

auto make_x11_keymap_conf(std::string_view layout,
    std::optional<std::string_view> model,
    std::optional<std::string_view> variant,
    std::optional<std::string_view> options) noexcept -> std::string {
    std::string body;
    body.reserve(256);
    body += "Section \"InputClass\"\n"sv;
    body += "    Identifier \"system-keyboard\"\n"sv;
    body += "    MatchIsKeyboard \"on\"\n"sv;
    body += fmt::format(FMT_COMPILE("    Option \"XkbLayout\" \"{}\"\n"), layout);
    if (model) {
        body += fmt::format(FMT_COMPILE("    Option \"XkbModel\" \"{}\"\n"), *model);
    }
    if (variant) {
        body += fmt::format(FMT_COMPILE("    Option \"XkbVariant\" \"{}\"\n"), *variant);
    }
    if (options) {
        body += fmt::format(FMT_COMPILE("    Option \"XkbOptions\" \"{}\"\n"), *options);
    }
    body += "EndSection\n"sv;
    return body;
}

auto apply(const Config& cfg, std::string_view root_mountpoint) noexcept -> Result<void> {
    const fs::path root{root_mountpoint};

    if (cfg.console_keymap) {
        const auto etc_dir       = root / "etc";
        const auto vconsole_path = (etc_dir / "vconsole.conf").string();
        std::error_code ec;
        fs::create_directories(etc_dir, ec);
        if (ec) {
            return make_error(ErrorCode::FileIo, fmt::format("keyboard: failed to create {}: {}", etc_dir.string(), ec.message()));
        }
        const auto body = make_vconsole_conf(*cfg.console_keymap, as_view(cfg.console_font));
        if (!file_utils::create_file_for_overwrite(vconsole_path, body)) {
            return make_error(ErrorCode::FileIo, fmt::format("keyboard: failed to write {}", vconsole_path));
        }
        spdlog::info("keyboard: vconsole.conf written to {}", vconsole_path);
    }

    if (cfg.x11_layout) {
        const auto xorg_dir  = root / "etc" / "X11" / "xorg.conf.d";
        const auto conf_path = (xorg_dir / "00-keyboard.conf").string();
        std::error_code ec;
        fs::create_directories(xorg_dir, ec);
        if (ec) {
            return make_error(ErrorCode::FileIo, fmt::format("keyboard: failed to create {}: {}", xorg_dir.string(), ec.message()));
        }
        const auto body = make_x11_keymap_conf(*cfg.x11_layout,
            as_view(cfg.x11_model), as_view(cfg.x11_variant), as_view(cfg.x11_options));
        if (!file_utils::create_file_for_overwrite(conf_path, body)) {
            return make_error(ErrorCode::FileIo, fmt::format("keyboard: failed to write {}", conf_path));
        }
        spdlog::info("keyboard: X11 keymap written to {}", conf_path);
    }

    return {};
}

}  // namespace gucc::keyboard
