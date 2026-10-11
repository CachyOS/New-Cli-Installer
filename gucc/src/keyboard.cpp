#include "gucc/keyboard.hpp"
#include "gucc/file_utils.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {

auto as_view(const std::optional<std::string>& opt) noexcept -> std::optional<std::string_view> {
    return opt.transform([](const std::string& str) { return std::string_view{str}; });
}

auto write_config(const fs::path& dir, std::string_view filename, std::string_view body) noexcept -> gucc::Result<void> {
    if (auto res = gucc::file_utils::create_directories(dir.native()); !res) {
        return res;
    }
    const auto path = (dir / filename).string();
    if (auto res = gucc::file_utils::write_file(path, body); !res) {
        return res;
    }
    spdlog::info("keyboard: {} written", path);
    return {};
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
    const auto etc_dir = fs::path{root_mountpoint} / "etc";

    if (cfg.console_keymap) {
        if (auto res = write_config(etc_dir, "vconsole.conf"sv, make_vconsole_conf(*cfg.console_keymap, as_view(cfg.console_font))); !res) {
            return res;
        }
    }

    if (cfg.x11_layout) {
        const auto body = make_x11_keymap_conf(*cfg.x11_layout,
            as_view(cfg.x11_model), as_view(cfg.x11_variant), as_view(cfg.x11_options));
        if (auto res = write_config(etc_dir / "X11" / "xorg.conf.d", "00-keyboard.conf"sv, body); !res) {
            return res;
        }
    }

    return {};
}

}  // namespace gucc::keyboard
