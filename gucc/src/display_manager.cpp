#include "gucc/display_manager.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/systemd_services.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {
using namespace gucc::display_manager;

constexpr std::array kPreferred = {
    Kind::Plasmalogin,
    Kind::Lightdm,
    Kind::Sddm,
    Kind::Gdm,
    Kind::Lxdm,
    Kind::Ly,
    Kind::CosmicGreeter,
};

constexpr auto kDefaultLightdmGreeter = "lightdm-gtk-greeter"sv;
}  // namespace

namespace gucc::display_manager {

auto known_kinds() noexcept -> std::span<const Kind> {
    return {kPreferred};
}

auto to_string(Kind k) noexcept -> std::string_view {
    switch (k) {
    case Kind::Gdm:
        return "gdm"sv;
    case Kind::Sddm:
        return "sddm"sv;
    case Kind::Lightdm:
        return "lightdm"sv;
    case Kind::Lxdm:
        return "lxdm"sv;
    case Kind::Ly:
        return "ly"sv;
    case Kind::Plasmalogin:
        return "plasmalogin"sv;
    case Kind::CosmicGreeter:
        return "cosmic-greeter"sv;
    }
    return {};
}

auto from_string(std::string_view name) noexcept -> std::optional<Kind> {
    const auto* const it = std::ranges::find(kPreferred, name, &to_string);
    if (it == kPreferred.end()) {
        return std::nullopt;
    }
    return *it;
}

auto detect_installed(std::string_view root_mountpoint) noexcept -> std::optional<Kind> {
    const auto* const it = std::ranges::find_if(kPreferred, [root_mountpoint](Kind k) noexcept {
        return services::systemd_unit_exists(to_string(k), root_mountpoint);
    });
    if (it == kPreferred.end()) {
        return std::nullopt;
    }
    return *it;
}

auto enable(Kind kind, std::string_view root_mountpoint) noexcept -> bool {
    const auto unit = to_string(kind);
    if (!services::systemd_unit_exists(unit, root_mountpoint)) {
        spdlog::debug("display_manager: unit '{}' not present on target, skipping enable", unit);
        return false;
    }
    if (!services::enable_systemd_service(unit, root_mountpoint)) {
        spdlog::error("display_manager: failed to enable '{}'", unit);
        return false;
    }
    spdlog::info("display_manager: enabled '{}'", unit);
    return true;
}

auto pick_lightdm_greeter(std::string_view xgreeters_dir) noexcept -> std::optional<std::string> {
    std::error_code ec;
    if (!fs::exists(xgreeters_dir, ec) || !fs::is_directory(xgreeters_dir, ec)) {
        return std::nullopt;
    }

    std::vector<std::string> candidates;
    for (const auto& entry : fs::directory_iterator{xgreeters_dir, ec}) {
        if (ec) {
            break;
        }
        const auto name = entry.path().filename().string();
        if (!name.starts_with("lightdm-"sv) || !name.ends_with("-greeter"sv)) {
            continue;
        }
        if (name == kDefaultLightdmGreeter) {
            continue;
        }
        candidates.push_back(name);
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(candidates);
    return candidates.front();
}

auto configure_lightdm_greeter(std::string_view root_mountpoint) noexcept -> bool {
    const auto xgreeters_dir = (fs::path{root_mountpoint} / "usr" / "share" / "xgreeters").string();
    const auto picked        = pick_lightdm_greeter(xgreeters_dir);
    if (!picked) {
        return true;
    }

    const auto conf_path = (fs::path{root_mountpoint} / "etc" / "lightdm" / "lightdm.conf").string();
    std::error_code ec;
    if (!fs::exists(conf_path, ec)) {
        spdlog::debug("display_manager: {} absent, skipping greeter config", conf_path);
        return true;
    }

    const auto contents = file_utils::read_whole_file(conf_path);
    std::string rewritten;
    rewritten.reserve(contents.size() + picked->size());

    const std::string_view view{contents};
    std::size_t pos = 0;
    while (pos <= view.size()) {
        const auto eol  = view.find('\n', pos);
        const auto end  = eol == std::string_view::npos ? view.size() : eol;
        const auto line = view.substr(pos, end - pos);
        if (line.contains("greeter-session="sv)) {
            rewritten += "greeter-session=";
            rewritten += *picked;
        } else {
            rewritten.append(line);
        }
        if (eol == std::string_view::npos) {
            break;
        }
        rewritten += '\n';
        pos = eol + 1;
    }

    if (!file_utils::create_file_for_overwrite(conf_path, rewritten)) {
        spdlog::error("display_manager: failed to write {}", conf_path);
        return false;
    }
    spdlog::info("display_manager: lightdm greeter set to '{}'", *picked);
    return true;
}

}  // namespace gucc::display_manager
