#include "cachyos/requirements.hpp"

#include "cachyos/system.hpp"
#include "cachyos/types.hpp"

#include "gucc/file_utils.hpp"
#include "gucc/memory.hpp"
#include "gucc/system_query.hpp"

#include <unistd.h>       // for getuid

#include <cstdint>  // for uint64_t

#include <algorithm>      // for max, find
#include <atomic>         // for atomic_bool
#include <filesystem>     // for directory_iterator
#include <mutex>          // for mutex, lock_guard
#include <ranges>         // for ranges::*
#include <string>         // for getline
#include <unordered_map>  // for unordered_map

#include <cpr/api.h>
#include <cpr/response.h>
#include <cpr/status_codes.h>
#include <cpr/timeout.h>

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using namespace std::string_view_literals;

// NOLINTNEXTLINE
using namespace cachyos::installer;

namespace {
constexpr auto kPowerSupplyPath      = "/sys/class/power_supply"sv;

// Calamares applies a ~5% margin when comparing reported totalram against the
// configured threshold.
constexpr double kRamMargin = 0.95;

struct Registry {
    std::mutex mtx;
    std::unordered_map<std::string, RequirementChecker> checkers;
    std::vector<std::string> insertion_order;
    std::atomic_bool builtins_registered{false};
};

auto registry() noexcept -> Registry& {
    static Registry r;
    return r;
}

auto bytes_to_gib(std::uint64_t bytes) noexcept -> double {
    return static_cast<double>(bytes) / static_cast<double>(gucc::memory::kGiB);
}
}  // namespace

// NOLINTBEGIN(misc-use-anonymous-namespace)
namespace builtin {

static auto root(const RequirementsConfig&) noexcept -> Requirement {
    Requirement r{};
    r.message_ok     = "Running as the administrator (root) user"sv;
    r.message_failed = "The installer must be run as the administrator (root) user"sv;
    r.satisfied      = (::getuid() == 0);
    return r;
}

static auto uefi(const RequirementsConfig&) noexcept -> Requirement {
    Requirement r{};
    const auto& sys    = detect_system();
    const bool is_uefi = sys && sys->system_mode == InstallContext::SystemMode::UEFI;
    r.message_ok       = "System is booted in UEFI mode"sv;
    r.message_failed   = "System is booted in BIOS/legacy mode"sv;
    r.satisfied        = is_uefi;
    return r;
}

static auto ram(const RequirementsConfig& cfg) noexcept -> Requirement {
    Requirement r{};

    const auto total_bytes = gucc::memory::total_bytes();
    if (total_bytes == 0) {
        spdlog::warn("requirements: cannot read total memory");
    }

    const auto threshold_bytes = static_cast<std::uint64_t>(
        cfg.required_ram_gib * static_cast<double>(gucc::memory::kGiB));

    r.message_ok = fmt::format(
        FMT_COMPILE("Has at least {:.1f} GiB of RAM ({:.1f} GiB detected)"),
        cfg.required_ram_gib, bytes_to_gib(total_bytes));
    r.message_failed = fmt::format(
        FMT_COMPILE("System needs at least {:.1f} GiB of RAM ({:.1f} GiB detected)"),
        cfg.required_ram_gib, bytes_to_gib(total_bytes));
    r.satisfied = static_cast<double>(total_bytes) >= static_cast<double>(threshold_bytes) * kRamMargin;
    return r;
}

static auto storage(const RequirementsConfig& cfg) noexcept -> Requirement {
    Requirement r{};

    std::uint64_t largest_bytes{};
    if (auto disks = gucc::disk::list_disks(); disks) {
        for (const auto& disk : *disks) {
            if (disk.is_removable) {
                continue;
            }
            largest_bytes = std::max(largest_bytes, disk.size);
        }
    } else {
        spdlog::warn("requirements: gucc::disk::list_disks() returned nullopt");
    }

    const auto threshold_bytes = static_cast<std::uint64_t>(
        cfg.required_storage_gib * static_cast<double>(gucc::memory::kGiB));

    r.message_ok = fmt::format(
        FMT_COMPILE("Has at least {:.1f} GiB of disk space ({:.1f} GiB available)"),
        cfg.required_storage_gib, bytes_to_gib(largest_bytes));
    r.message_failed = fmt::format(
        FMT_COMPILE("System needs at least {:.1f} GiB of disk space ({:.1f} GiB available)"),
        cfg.required_storage_gib, bytes_to_gib(largest_bytes));
    r.satisfied = largest_bytes >= threshold_bytes;
    return r;
}

static auto power(const RequirementsConfig&) noexcept -> Requirement {
    Requirement r{};
    r.message_ok     = "System is plugged into AC power";
    r.message_failed = "System is running on battery power";

    bool has_battery = false;
    bool on_ac       = false;
    std::error_code ec{};
    if (!fs::exists(kPowerSupplyPath, ec)) {
        r.satisfied = true;
        return r;
    }
    for (const auto& entry : fs::directory_iterator{kPowerSupplyPath, ec}) {
        const auto type = gucc::file_utils::read_first_line((entry.path() / "type").native());
        if (type == "Battery"sv) {
            // Skip peripherals
            if (gucc::file_utils::read_first_line((entry.path() / "scope").native()) == "Device"sv) {
                continue;
            }
            has_battery = true;
        } else if (type == "Mains"sv || type == "USB"sv || type == "USB_PD"sv) {
            if (gucc::file_utils::read_first_line((entry.path() / "online").native()) == "1") {
                on_ac = true;
            }
        }
    }
    r.satisfied = !has_battery || on_ac;
    return r;
}

static auto internet(const RequirementsConfig& cfg) noexcept -> Requirement {
    Requirement r{};
    r.message_ok = fmt::format(
        FMT_COMPILE("Internet connection reachable ({})"), cfg.internet_check_url);
    r.message_failed = fmt::format(
        FMT_COMPILE("Internet connection unreachable ({})"), cfg.internet_check_url);

    const auto resp   = cpr::Get(cpr::Url{cfg.internet_check_url}, cpr::Timeout{15s});
    const auto status = static_cast<std::int32_t>(resp.status_code);
    r.satisfied       = cpr::status::is_success(status) || cpr::status::is_redirect(status);
    return r;
}

}  // namespace builtin
// NOLINTEND(misc-use-anonymous-namespace)

namespace cachyos::installer {

void register_requirement(std::string_view id, RequirementChecker checker) noexcept {
    auto& reg = registry();
    const std::lock_guard lock{reg.mtx};

    std::string id_str{id};
    if (!reg.checkers.contains(id_str)) {
        reg.insertion_order.push_back(id_str);
    }
    reg.checkers[std::move(id_str)] = std::move(checker);
}

auto registered_requirement_ids() noexcept -> std::vector<std::string> {
    auto& reg = registry();
    const std::lock_guard lock{reg.mtx};
    return reg.insertion_order;
}

auto check_requirements(const RequirementsConfig& cfg) noexcept -> std::vector<Requirement> {
    std::vector<std::pair<std::string, RequirementChecker>> snapshot;
    {
        auto& reg = registry();
        const std::lock_guard lock{reg.mtx};
        snapshot.reserve(cfg.checks.size());
        for (const auto& id : cfg.checks) {
            if (const auto it = reg.checkers.find(id); it != reg.checkers.end()) {
                snapshot.emplace_back(id, it->second);
            } else {
                spdlog::warn("requirements: no checker registered for '{}'", id);
            }
        }
    }

    std::vector<Requirement> out;
    out.reserve(snapshot.size());
    for (const auto& [id, checker] : snapshot) {
        auto r      = checker(cfg);
        r.id        = id;
        r.mandatory = std::ranges::contains(cfg.mandatory, id);
        out.push_back(std::move(r));
    }
    return out;
}

void register_builtin_requirements() noexcept {
    auto& reg = registry();
    if (reg.builtins_registered.exchange(true)) {
        return;
    }
    register_requirement("root", &builtin::root);
    register_requirement("uefi", &builtin::uefi);
    register_requirement("ram", &builtin::ram);
    register_requirement("storage", &builtin::storage);
    register_requirement("power", &builtin::power);
    register_requirement("internet", &builtin::internet);
}

}  // namespace cachyos::installer
