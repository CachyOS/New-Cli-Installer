#include "gucc/luks_swap.hpp"
#include "gucc/block_devices.hpp"
#include "gucc/conf.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>    // for path
#include <functional>    // for function_ref
#include <ranges>        // for ranges::*
#include <string>        // for string
#include <string_view>   // for string_view
#include <system_error>  // for error_code

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {

using DropClause = std::function_ref<bool(std::string_view)>;

auto rewrite_file_with_entry(std::string_view path, DropClause should_drop, std::string_view new_line) noexcept -> gucc::Result<void> {
    if (auto res = gucc::file_utils::create_directories(fs::path{path}.parent_path().string()); !res) {
        return res;
    }

    std::error_code ec;
    const auto existing = fs::exists(path, ec) ? gucc::file_utils::read_whole_file(path) : std::string{};
    return gucc::file_utils::write_file(path, gucc::luks_swap::detail::rewrite_with_entry(existing, should_drop, new_line));
}

}  // namespace

namespace gucc::luks_swap::detail {

auto line_owns_name(std::string_view line, std::string_view name) noexcept -> bool {
    // crypttab fields split on blanks only
    return conf::active(line).transform(utils::first_field) == name;
}

auto is_swap_type_line(std::string_view line) noexcept -> bool {
    return conf::active(line).transform([](std::string_view body) { return utils::nth_field(body, 2); }) == "swap"sv;
}

auto rewrite_with_entry(std::string_view content, DropClause should_drop, std::string_view new_line) -> std::string {
    auto kept = utils::split_lines(content)
        | std::ranges::views::filter([should_drop](std::string_view line) { return !line.empty() && !should_drop(line); });
    return std::ranges::views::concat(kept, std::ranges::views::single(new_line))
        | std::ranges::views::join_with('\n')
        | std::ranges::to<std::string>();
}

}  // namespace gucc::luks_swap::detail

namespace gucc::luks_swap {

auto make_crypttab_line(const RandomKeyConfig& cfg) noexcept -> std::string {
    auto line = fmt::format(FMT_COMPILE("{} {} /dev/urandom swap,cipher={},size={}"),
        cfg.mapper_name, cfg.source_device, cfg.cipher, cfg.key_size);
    if (cfg.sector_size != 0U) {
        line += fmt::format(FMT_COMPILE(",sector-size={}"), cfg.sector_size);
    }
    line += '\n';
    return line;
}

auto make_fstab_line(std::string_view mapper_name) noexcept -> std::string {
    return fmt::format(FMT_COMPILE("/dev/mapper/{} none swap defaults 0 0\n"), mapper_name);
}

auto add_crypttab_entry(const RandomKeyConfig& cfg,
    std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto path = utils::target_path(root_mountpoint, "/etc/crypttab"sv);
    return rewrite_file_with_entry(path, [name = cfg.mapper_name](std::string_view line) { return detail::line_owns_name(line, name); }, make_crypttab_line(cfg));
}

auto add_fstab_entry(std::string_view mapper_name,
    std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto path  = utils::target_path(root_mountpoint, "/etc/fstab"sv);
    const auto first = disk::mapper_path(mapper_name);
    return rewrite_file_with_entry(path, [&first](std::string_view line) { return detail::line_owns_name(line, first); }, make_fstab_line(mapper_name));
}

auto replace_swap_in_fstab(std::string_view mapper_name,
    std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto path = utils::target_path(root_mountpoint, "/etc/fstab"sv);
    return rewrite_file_with_entry(path, detail::is_swap_type_line, make_fstab_line(mapper_name));
}

}  // namespace gucc::luks_swap
