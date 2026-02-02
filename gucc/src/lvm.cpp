#include "gucc/lvm.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <ranges>  // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

/// Parse LVM output lines, trimming whitespace and filtering empty lines
auto parse_lvm_output(std::string_view output) noexcept -> std::vector<std::string> {
    return gucc::utils::make_multiline_view(output)
        | std::ranges::views::transform([](std::string_view sv) { return gucc::utils::trim(sv); })
        | std::ranges::views::filter([](std::string_view sv) { return !sv.empty(); })
        | std::ranges::views::transform([](std::string_view sv) { return std::string{sv}; })
        | std::ranges::to<std::vector<std::string>>();
}

}  // namespace

namespace gucc::lvm {

auto detect_lvm() noexcept -> LvmInfo {
    LvmInfo info{};

    // Get physical volumes using --noheading for clean output
    const auto& pv_output = utils::exec("pvs -o pv_name --noheading 2>/dev/null");
    if (!pv_output.empty()) {
        info.physical_volumes = parse_lvm_output(pv_output);
    }

    // Get volume groups
    const auto& vg_output = utils::exec("vgs -o vg_name --noheading 2>/dev/null");
    if (!vg_output.empty()) {
        info.volume_groups = parse_lvm_output(vg_output);
    }

    // Get logical volumes as vg-lv pairs
    const auto& lv_output = utils::exec("lvs -o vg_name,lv_name --noheading --separator='-' 2>/dev/null");
    if (!lv_output.empty()) {
        info.logical_volumes = parse_lvm_output(lv_output);
    }

    return info;
}

auto activate_lvm() noexcept -> bool {
    // Load dm-mod kernel module
    if (!utils::exec_checked("modprobe -v dm-mod &>>/tmp/cachyos-install.log")) {
        spdlog::error("Failed to load dm-mod kernel module");
        return false;
    }

    // Scan for volume groups
    if (!utils::exec_checked("vgscan -v 1>/dev/null 2>>/tmp/cachyos-install.log")) {
        spdlog::error("Failed to scan for volume groups");
        return false;
    }

    // Activate all volume groups
    if (!utils::exec_checked("vgchange -ay -v 1>/dev/null 2>>/tmp/cachyos-install.log")) {
        spdlog::error("Failed to activate logical volumes");
        return false;
    }

    spdlog::info("LVM volumes activated successfully");
    return true;
}

}  // namespace gucc::lvm
