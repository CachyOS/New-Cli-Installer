#include "gucc/chwd.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for contains, for_each
#include <ranges>     // for ranges::*

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::chwd {

auto get_all_profile_names() noexcept -> std::vector<std::string> {
    const auto& profiles = utils::exec("chwd --list-all | grep -v 'Name' | grep '│' | awk '{print $2}'"sv);
    return utils::make_multiline(profiles);
}

auto get_available_profile_names() noexcept -> std::vector<std::string> {
    auto&& all_profile_names = get_all_profile_names();
    /* clang-format off */
    if (all_profile_names.empty()) { return {}; }
    /* clang-format on */

    const auto& available_profile_names = utils::make_multiline(utils::exec("chwd --list -d | grep Name | awk '{print $4}'"sv));

    std::vector<std::string> filtered_profile_names{};
    const auto& functor = [available_profile_names](auto&& profile_name) { return std::ranges::contains(available_profile_names, profile_name); };

    const auto& for_each_functor = [&](auto&& rng) { filtered_profile_names.push_back(std::forward<decltype(rng)>(rng)); };
    std::ranges::for_each(all_profile_names | std::ranges::views::filter(functor), for_each_functor);
    return filtered_profile_names;
}

auto install_available_profiles(std::string_view root_mountpoint) noexcept -> bool {
    if (!utils::arch_chroot_checked("chwd -a -f"sv, root_mountpoint)) {
        spdlog::error("Failed to run chwd -a -f on {}", root_mountpoint);
        return false;
    }
    return true;
}

}  // namespace gucc::chwd
