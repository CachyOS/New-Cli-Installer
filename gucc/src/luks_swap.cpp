#include "gucc/luks_swap.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {
using namespace gucc;

auto line_owns_name(std::string_view line, std::string_view name) noexcept -> bool {
    const auto trimmed = utils::ltrim(line);
    if (trimmed.empty() || trimmed.starts_with('#') || !trimmed.starts_with(name)) {
        return false;
    }
    if (trimmed.size() == name.size()) {
        return true;
    }
    const auto next = trimmed[name.size()];
    return next == ' ' || next == '\t';
}

auto field_at(std::string_view line, std::size_t index) noexcept -> std::string_view {
    auto rest = utils::ltrim(line);
    for (std::size_t i = 0; i < index; ++i) {
        const auto sp = rest.find_first_of(" \t"sv);
        if (sp == std::string_view::npos) {
            return {};
        }
        rest = utils::ltrim(rest.substr(sp));
    }
    const auto sp = rest.find_first_of(" \t"sv);
    return rest.substr(0, sp == std::string_view::npos ? rest.size() : sp);
}

auto is_swap_type_line(std::string_view line) noexcept -> bool {
    const auto trimmed = utils::ltrim(line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
        return false;
    }
    return field_at(trimmed, 2) == "swap"sv;
}

template <typename ShouldDrop>
auto rewrite_with_entry(std::string_view path,
    ShouldDrop should_drop,
    std::string_view new_line) noexcept -> Result<void> {
    const fs::path target{path};
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) {
        return make_error(ErrorCode::FileIo, fmt::format("luks_swap: failed to create {}: {}", target.parent_path().string(), ec.message()));
    }

    std::string existing;
    if (fs::exists(target, ec)) {
        existing = file_utils::read_whole_file(path);
    }

    std::string rewritten;
    rewritten.reserve(existing.size() + new_line.size());
    auto kept = std::ranges::views::split(std::string_view{existing}, '\n')
        | std::ranges::views::transform([](auto&& chunk) { return std::string_view{chunk}; })
        | std::ranges::views::filter([&should_drop](std::string_view line) {
              return !line.empty() && !should_drop(line);
          });
    for (const auto line : kept) {
        rewritten.append(line);
        rewritten += '\n';
    }
    rewritten.append(new_line);

    if (!file_utils::create_file_for_overwrite(path, rewritten)) {
        return make_error(ErrorCode::FileIo, fmt::format("luks_swap: failed to write {}", std::string{path}));
    }
    return {};
}

}  // namespace

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
    const auto path = (fs::path{root_mountpoint} / "etc" / "crypttab").string();
    return rewrite_with_entry(path, [name = cfg.mapper_name](std::string_view line) { return line_owns_name(line, name); }, make_crypttab_line(cfg));
}

auto add_fstab_entry(std::string_view mapper_name,
    std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto path  = (fs::path{root_mountpoint} / "etc" / "fstab").string();
    const auto first = fmt::format(FMT_COMPILE("/dev/mapper/{}"), mapper_name);
    return rewrite_with_entry(path, [&first](std::string_view line) { return line_owns_name(line, first); }, make_fstab_line(mapper_name));
}

auto replace_swap_in_fstab(std::string_view mapper_name,
    std::string_view root_mountpoint) noexcept -> Result<void> {
    const auto path = (fs::path{root_mountpoint} / "etc" / "fstab").string();
    return rewrite_with_entry(path, is_swap_type_line, make_fstab_line(mapper_name));
}

}  // namespace gucc::luks_swap
