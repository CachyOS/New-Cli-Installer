#include "chwd_profiles.hpp"
#include "utils.hpp"

// import gucc
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

#include <algorithm>  // for contains, sort, for_each
#include <ranges>     // for ranges::*

#include <fmt/compile.h>
#include <fmt/format.h>

#define TOML_EXCEPTIONS 0  // disable exceptions
#include <toml++/toml.h>

namespace detail::chwd {

auto get_all_profile_names(std::string_view profile_type) noexcept -> std::optional<std::vector<std::string>> {
    const auto& file_path     = fmt::format(FMT_COMPILE("/var/lib/chwd/db/pci/{}/profiles.toml"), profile_type);
    toml::parse_result config = toml::parse_file(file_path);
    /* clang-format off */
    if (config.failed()) { return {}; }
    /* clang-format on */

    std::vector<std::string> profile_names{};
    for (auto&& [key, value] : config.table()) {
        if (value.is_table()) {
            for (auto&& [nested_key, nested_value] : *value.as_table()) {
                /* clang-format off */
                if (!nested_value.is_table()) { continue; }
                /* clang-format on */

                const auto& nested_profile_name = fmt::format(FMT_COMPILE("{}.{}"), std::string_view{key}, std::string_view{nested_key});
                profile_names.push_back(nested_profile_name);
            }
            profile_names.emplace_back(key);
        }
    }

    return profile_names;
}

auto get_available_profile_names(std::string_view profile_type) noexcept -> std::optional<std::vector<std::string>> {
    auto&& all_profile_names = get_all_profile_names(profile_type);
    /* clang-format off */
    if (!all_profile_names.has_value()) { return {}; }
    /* clang-format on */

    const auto& available_profile_names = gucc::utils::make_multiline(gucc::utils::exec("chwd --list -d | grep Name | awk '{print $4}'"));

    std::vector<std::string> filtered_profile_names{};
    const auto& functor = [available_profile_names](auto&& profile_name) { return std::ranges::contains(available_profile_names, profile_name); };

    const auto& for_each_functor = [&](auto&& rng) { filtered_profile_names.push_back(std::forward<decltype(rng)>(rng)); };
    std::ranges::for_each(all_profile_names.value() | std::ranges::views::filter(functor), for_each_functor);
    return filtered_profile_names;
}

}  // namespace detail::chwd
