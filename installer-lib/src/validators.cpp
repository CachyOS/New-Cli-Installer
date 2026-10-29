#include "cachyos/validators.hpp"

// import gucc
#include "gucc/memory.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for contains, none_of
#include <array>      // for array
#include <ranges>     // for ranges::*

#include <ctre.hpp>

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {

constexpr auto is_dot_component(std::string_view component) noexcept -> bool {
    return component == "."sv || component == ".."sv;
}

constexpr auto has_dot_component(std::string_view path) noexcept -> bool {
    return std::ranges::any_of(gucc::utils::split_lines(path, '/'), is_dot_component);
}

// ZFS_MAX_DATASET_NAME_LEN
constexpr std::size_t kZfsMaxNameLen = 255;

}  // namespace

namespace cachyos::installer {

auto validate_hostname(std::string_view hostname) noexcept -> std::expected<void, std::string> {
    static constexpr std::size_t kHostNameMax = 64;
    static constexpr std::size_t kLabelMax    = 63;

    if (hostname.empty()) {
        return std::unexpected("hostname is empty"s);
    }
    if (hostname.size() > kHostNameMax) {
        return std::unexpected(fmt::format(FMT_COMPILE("hostname is longer than {} characters"), kHostNameMax));
    }
    for (const auto label : gucc::utils::split_lines(hostname, '.')) {
        if (label.empty()) {
            return std::unexpected("hostname has an empty label"s);
        }
        if (label.size() > kLabelMax) {
            return std::unexpected(fmt::format(FMT_COMPILE("hostname label '{}' is longer than {} characters"), label, kLabelMax));
        }
        if (!ctre::match<"[A-Za-z0-9\\-]+">(label)) {
            return std::unexpected(fmt::format(FMT_COMPILE("hostname label '{}' may only contain A-Z, a-z, 0-9 and '-'"), label));
        }
        if (label.starts_with('-') || label.ends_with('-')) {
            return std::unexpected(fmt::format(FMT_COMPILE("hostname label '{}' starts or ends with '-'"), label));
        }
    }
    return {};
}

auto validate_username(std::string_view username) noexcept -> std::expected<void, std::string> {
    static constexpr std::size_t kUserNameMax = 32;
    // accounts from filesystem
    static constexpr std::array kTaken{"root"sv, "bin"sv, "daemon"sv, "mail"sv, "ftp"sv, "http"sv, "nobody"sv};

    if (username.empty()) {
        return std::unexpected("username is empty"s);
    }
    if (username.size() > kUserNameMax) {
        return std::unexpected(fmt::format(FMT_COMPILE("username is longer than {} characters"), kUserNameMax));
    }
    if (!ctre::starts_with<"[a-z_]">(username)) {
        return std::unexpected("username must start with a lowercase letter or '_'"s);
    }
    if (!ctre::match<"[a-z_][a-z0-9_\\-]*[$]?">(username)) {
        return std::unexpected("username may only contain a-z, 0-9, '_', '-' and a trailing '$'"s);
    }
    if (std::ranges::contains(kTaken, username)) {
        return std::unexpected(fmt::format(FMT_COMPILE("username '{}' is reserved by the base system"), username));
    }
    return {};
}

auto is_valid_zpool_name(std::string_view name) noexcept -> bool {
    // vdev keywords would collide
    return name.size() <= kZfsMaxNameLen
        && ctre::match<"[A-Za-z][A-Za-z0-9_.:\\-]*">(name)
        && !ctre::starts_with<"log|mirror|raidz|draid|spare|c[0-9]">(name);
}

auto is_valid_zfs_dataset_name(std::string_view name) noexcept -> bool {
    return name.size() <= kZfsMaxNameLen
        && ctre::match<"[A-Za-z0-9_.:\\-]+(?:/[A-Za-z0-9_.:\\-]+)*">(name)
        && !has_dot_component(name);
}

auto is_valid_zfs_property(std::string_view assignment) noexcept -> bool {
    // native `[a-z][a-z0-9_]*` or user `module:name` over [a-z0-9:+._-]
    // https://openzfs.github.io/openzfs-docs/man/master/7/zfsprops.7.html
    const auto [whole, user] = ctre::match<"(?:[a-z][a-z0-9_]*|([a-z0-9+._\\-]*:[a-z0-9:+._\\-]*))=[A-Za-z0-9/._:+@,=\\-]+">(assignment);
    return whole && user.size() <= 256;
}

auto is_valid_zvol_size_mib(std::string_view size) noexcept -> bool {
    return gucc::utils::parse_uint_exact<std::uint64_t>(size).value_or(0) > 0;
}

auto parse_size(std::string_view size) noexcept -> std::optional<std::uint64_t> {
    const auto [whole, digits, unit] = ctre::match<"([0-9]+)(?:([KMGTkmgt])(?:[iI]?[bB])?|[bB])?">(size);
    if (!whole) {
        return std::nullopt;
    }
    const auto value = gucc::utils::parse_uint_exact<std::uint64_t>(digits.to_view());
    if (!value) {
        return std::nullopt;
    }
    const auto multiplier = unit ? gucc::memory::unit_multiplier(unit.to_view().front()).value_or(1) : std::uint64_t{1};
    return gucc::memory::scale_bytes(*value, multiplier);
}

auto is_valid_mountpoint(std::string_view mountpoint) noexcept -> bool {
    return ctre::match<"(?:/[A-Za-z0-9._@+\\-]+)+">(mountpoint) && !has_dot_component(mountpoint.substr(1));
}

}  // namespace cachyos::installer
