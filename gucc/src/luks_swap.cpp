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

auto rewrite_with_entry(std::string_view path,
    std::string_view first_field,
    std::string_view new_line) noexcept -> bool {
    const fs::path target{path};
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) {
        spdlog::error("luks_swap: failed to create {}: {}", target.parent_path().string(), ec.message());
        return false;
    }

    std::string existing;
    if (fs::exists(target, ec)) {
        existing = file_utils::read_whole_file(path);
    }

    std::string rewritten;
    rewritten.reserve(existing.size() + new_line.size());
    auto kept = std::ranges::views::split(std::string_view{existing}, '\n')
        | std::ranges::views::transform([](auto&& chunk) { return std::string_view{chunk}; })
        | std::ranges::views::filter([first_field](std::string_view line) {
              return !line.empty() && !line_owns_name(line, first_field);
          });
    for (const auto line : kept) {
        rewritten.append(line);
        rewritten += '\n';
    }
    rewritten.append(new_line);

    if (!file_utils::create_file_for_overwrite(path, rewritten)) {
        spdlog::error("luks_swap: failed to write {}", std::string{path});
        return false;
    }
    return true;
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
    std::string_view root_mountpoint) noexcept -> bool {
    const auto path = (fs::path{root_mountpoint} / "etc" / "crypttab").string();
    return rewrite_with_entry(path, cfg.mapper_name, make_crypttab_line(cfg));
}

auto add_fstab_entry(std::string_view mapper_name,
    std::string_view root_mountpoint) noexcept -> bool {
    const auto path  = (fs::path{root_mountpoint} / "etc" / "fstab").string();
    const auto first = fmt::format(FMT_COMPILE("/dev/mapper/{}"), mapper_name);
    return rewrite_with_entry(path, first, make_fstab_line(mapper_name));
}

}  // namespace gucc::luks_swap
