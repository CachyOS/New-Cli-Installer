#include "gucc/lvm.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <array>        // for array
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for make_pair, move, pair
#include <vector>       // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::lvm::detail {

auto parse_simple_lvm_output(std::string_view output) noexcept -> std::vector<std::string> {
    return utils::make_multiline_view(output)
        | std::ranges::views::transform(utils::trim)
        | std::ranges::views::filter([](std::string_view sv) { return !sv.empty(); })
        | std::ranges::to<std::vector<std::string>>();
}

}  // namespace gucc::lvm::detail

namespace gucc::lvm {

auto parse_vgs_output(std::string_view output) noexcept -> std::vector<std::pair<std::string, std::string>> {
    if (output.empty()) {
        return {};
    }

    return utils::make_multiline_view(output)
        | std::ranges::views::transform([](std::string_view line) {
              auto trimmed = utils::trim(line);
              auto split   = utils::make_split_view(trimmed, '\t');
              auto it      = std::ranges::begin(split);

              if (it == std::ranges::end(split)) {
                  return std::pair<std::string, std::string>{};
              }
              auto vg_name = std::string{utils::trim(*it)};
              ++it;

              if (it == std::ranges::end(split)) {
                  return std::make_pair(std::move(vg_name), std::string{});
              }
              auto vg_size = std::string{utils::trim(*it)};

              return std::make_pair(std::move(vg_name), std::move(vg_size));
          })
        | std::ranges::views::filter([](auto&& pair) { return !pair.first.empty(); })
        | std::ranges::to<std::vector<std::pair<std::string, std::string>>>();
}

auto detect_lvm() noexcept -> LvmInfo {
    const auto query_list = [](std::string_view command) -> std::vector<std::string> {
        return utils::query_output(command).transform(detail::parse_simple_lvm_output).value_or(std::vector<std::string>{});
    };
    return LvmInfo{
        .physical_volumes = query_list("pvs -o pv_name --noheading 2>/dev/null"sv),
        .volume_groups    = query_list("vgs -o vg_name --noheading 2>/dev/null"sv),
        // vg-lv pairs
        .logical_volumes = query_list("lvs -o vg_name,lv_name --noheading --separator='-' 2>/dev/null"sv),
    };
}

auto activate_lvm() noexcept -> Result<void> {
    static constexpr std::array steps{
        std::pair{"modprobe -v dm-mod"sv, "load dm-mod kernel module"sv},
        std::pair{"vgscan -v"sv, "scan for volume groups"sv},
        std::pair{"vgchange -ay -v"sv, "activate logical volumes"sv},
    };
    for (const auto& [cmd, what] : steps) {
        if (auto res = utils::exec_or_error(cmd, what); !res) {
            return res;
        }
    }

    spdlog::info("LVM volumes activated successfully");
    return {};
}

auto show_volume_groups() noexcept -> std::vector<std::pair<std::string, std::string>> {
    return utils::query_output("vgs -o vg_name,vg_size --noheading --separator='\t' --units=g 2>/dev/null"sv)
        .transform(parse_vgs_output)
        .value_or(std::vector<std::pair<std::string, std::string>>{});
}

}  // namespace gucc::lvm
