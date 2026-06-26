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

#include <fmt/format.h>
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

// TODO(vnepogodin): refactor using staticmap
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

auto enable(Kind kind, std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto unit = to_string(kind);
    if (!services::systemd_unit_exists(unit, root_mountpoint)) {
        return make_error(ErrorCode::NotFound, fmt::format("display_manager: unit '{}' not present on target, skipping enable", unit));
    }
    if (!services::enable_systemd_service(unit, root_mountpoint)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("display_manager: failed to enable '{}'", unit));
    }
    spdlog::info("display_manager: enabled '{}'", unit);
    return {};
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

auto configure_lightdm_greeter(std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto xgreeters_dir = (fs::path{root_mountpoint} / "usr" / "share" / "xgreeters").string();
    const auto picked        = pick_lightdm_greeter(xgreeters_dir);
    if (!picked) {
        return {};
    }

    const auto conf_path = (fs::path{root_mountpoint} / "etc" / "lightdm" / "lightdm.conf").string();
    std::error_code ec;
    if (!fs::exists(conf_path, ec)) {
        spdlog::debug("display_manager: {} absent, skipping greeter config", conf_path);
        return {};
    }

    const auto contents = file_utils::read_whole_file(conf_path);
    std::string rewritten;
    rewritten.reserve(contents.size() + picked->size());

    auto transformed = std::ranges::views::split(std::string_view{contents}, '\n')
        | std::ranges::views::transform([](auto&& chunk) { return std::string_view{chunk}; })
        | std::ranges::views::filter([](std::string_view line) { return !line.empty(); })
        | std::ranges::views::transform([&picked](std::string_view line) -> std::string {
              if (line.contains("greeter-session="sv)) {
                  return fmt::format("greeter-session={}", *picked);
              }
              return std::string{line};
          });
    for (const auto& line : transformed) {
        rewritten.append(line);
        rewritten += '\n';
    }

    if (!file_utils::create_file_for_overwrite(conf_path, rewritten)) {
        return make_error(ErrorCode::FileIo, fmt::format("display_manager: failed to write {}", conf_path));
    }
    spdlog::info("display_manager: lightdm greeter set to '{}'", *picked);
    return {};
}

}  // namespace gucc::display_manager
