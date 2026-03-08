#include "installer_data.hpp"

// import gucc
#include "gucc/string_utils.hpp"
#include "gucc/system_query.hpp"

#include <string>   // for string
#include <utility>  // for move
#include <vector>   // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

namespace installer::data {

auto get_device_list() noexcept -> std::vector<std::string> {
    auto disks = gucc::disk::list_disks();
    if (!disks) {
        return {};
    }

    std::vector<std::string> result;
    result.reserve(disks->size());
    for (const auto& disk : *disks) {
        auto dev_size = gucc::disk::format_size(disk.size);
        auto res_str  = fmt::format(FMT_COMPILE("{} {}"), disk.device, dev_size);
        result.emplace_back(std::move(res_str));
    }
    return result;
}

auto parse_device_name(std::string_view entry) noexcept -> std::string {
    const auto& parts = gucc::utils::make_multiline(entry, false, ' ');
    return parts.empty() ? std::string{} : parts[0];
}

}  // namespace installer::data
