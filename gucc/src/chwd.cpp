#include "gucc/chwd.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>    // for contains
#include <optional>     // for optional
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

#include <fmt/format.h>

using namespace std::string_view_literals;

namespace {

struct TableRow {
    std::string_view key;
    std::string_view value;
};

// chwd prints two-column tables
constexpr auto parse_table_row(std::string_view line) noexcept -> std::optional<TableRow> {
    // TODO(vnepogodin): why not just --json?
    constexpr auto kBorder    = "│"sv;
    constexpr auto kSeparator = "┆"sv;

    line = gucc::utils::trim(line);
    if ((line.size() < 2 * kBorder.size()) || !line.starts_with(kBorder) || !line.ends_with(kBorder)) {
        return std::nullopt;
    }
    line.remove_prefix(kBorder.size());
    line.remove_suffix(kBorder.size());

    const auto separator = line.find(kSeparator);
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }
    return TableRow{
        .key   = gucc::utils::trim(line.substr(0, separator)),
        .value = gucc::utils::trim(line.substr(separator + kSeparator.size())),
    };
}

auto table_rows(std::string_view output) noexcept {
    return gucc::utils::make_split_view(output)
        | std::ranges::views::transform(parse_table_row)
        | std::ranges::views::join;
}

}  // namespace

namespace gucc::chwd::detail {

auto parse_profile_table(std::string_view output) noexcept -> std::vector<std::string> {
    return table_rows(output)
        | std::ranges::views::filter([](const TableRow& row) { return row.key != "Name"sv; })
        | std::ranges::views::transform([](const TableRow& row) { return std::string{row.key}; })
        | std::ranges::to<std::vector<std::string>>();
}

auto parse_profile_details(std::string_view output) noexcept -> std::vector<std::string> {
    return table_rows(output)
        | std::ranges::views::filter([](const TableRow& row) { return row.key == "Name"sv; })
        | std::ranges::views::transform([](const TableRow& row) { return std::string{row.value}; })
        | std::ranges::to<std::vector<std::string>>();
}

}  // namespace gucc::chwd::detail

namespace gucc::chwd {

auto get_all_profile_names() noexcept -> std::vector<std::string> {
    return detail::parse_profile_table(utils::exec("chwd --list-all"sv));
}

auto get_available_profile_names() noexcept -> std::vector<std::string> {
    auto&& all_profile_names = get_all_profile_names();
    /* clang-format off */
    if (all_profile_names.empty()) { return {}; }
    /* clang-format on */

    const auto& available_profile_names = detail::parse_profile_details(utils::exec("chwd --list -d"sv));
    return all_profile_names
        | std::ranges::views::filter([&](auto&& name) { return std::ranges::contains(available_profile_names, name); })
        | std::ranges::to<std::vector<std::string>>();
}

auto install_available_profiles(std::string_view root_mountpoint) noexcept -> Result<void> {
    if (!utils::arch_chroot_checked("chwd -a -f"sv, root_mountpoint)) {
        return make_error(ErrorCode::SubprocessFailed, fmt::format("Failed to run chwd -a -f on {}", root_mountpoint));
    }
    return {};
}

}  // namespace gucc::chwd
