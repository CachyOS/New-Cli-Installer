#include "gucc/display_manager.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/systemd_services.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fmt/compile.h>
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
    return utils::find_optional(kPreferred, name, &to_string);
}

auto detect_installed(std::string_view root_mountpoint) noexcept -> std::optional<Kind> {
    return utils::find_if_optional(kPreferred, [root_mountpoint](Kind k) noexcept { return services::systemd_unit_exists(to_string(k), root_mountpoint); });
}

auto enable(Kind kind, std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto unit = to_string(kind);
    if (!services::systemd_unit_exists(unit, root_mountpoint)) {
        return make_error(ErrorCode::NotFound, FMT_COMPILE("display_manager: unit '{}' not present on target, skipping enable"), unit);
    }
    if (auto res = services::enable_systemd_service(unit, root_mountpoint); !res) {
        return res;
    }
    spdlog::info("display_manager: enabled '{}'", unit);
    return {};
}

auto pick_lightdm_greeter(std::string_view xgreeters_dir) noexcept -> std::optional<std::string> {
    std::error_code ec;
    if (!fs::exists(xgreeters_dir, ec) || !fs::is_directory(xgreeters_dir, ec)) {
        return std::nullopt;
    }

    const auto candidates = fs::directory_iterator{xgreeters_dir, ec}
        | std::ranges::views::transform([](const fs::directory_entry& entry) { return entry.path().filename().string(); })
        | std::ranges::views::filter([](std::string_view name) {
              return name.starts_with("lightdm-"sv) && name.ends_with("-greeter"sv) && name != kDefaultLightdmGreeter;
          })
        | std::ranges::to<std::vector<std::string>>();
    if (candidates.empty()) {
        return std::nullopt;
    }
    return std::ranges::min(candidates);
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

    auto res = file_utils::rewrite_lines(conf_path, [&picked](std::string_view line) -> std::string {
        if (line.contains("greeter-session="sv)) {
            return fmt::format(FMT_COMPILE("greeter-session={}"), *picked);
        }
        return std::string{line};
    });
    if (!res) {
        return res;
    }
    spdlog::info("display_manager: lightdm greeter set to '{}'", *picked);
    return {};
}

}  // namespace gucc::display_manager
