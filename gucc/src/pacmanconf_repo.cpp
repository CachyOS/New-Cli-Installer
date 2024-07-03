#include "gucc/pacmanconf_repo.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

#include <fstream>  // for ofstream
#include <string>   // for string
#include <vector>   // for vector

#include <spdlog/spdlog.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/split.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace gucc::detail::pacmanconf {

bool push_repos_front(std::string_view file_path, std::string_view value) noexcept {
    auto&& file_content = file_utils::read_whole_file(file_path);
    if (file_content.empty()) {
        spdlog::error("[PACMANCONFREPO] '{}' error occurred!", file_path);
        return false;
    }

    auto file_split_view = utils::make_split_view(file_content);
    // Find the insertion point (using ranges for iteration)
    auto insertion_point_it = ranges::find_if(
        file_split_view,
        [](auto&& rng) {
            auto&& line = std::string_view(&*rng.begin(), static_cast<size_t>(ranges::distance(rng)));
            return !line.empty() && !line.starts_with('#') && line.starts_with('[') && !line.starts_with("[options]");
        });

    // Handle case where insertion point is not found
    if (insertion_point_it == ranges::end(file_split_view)) {
        // No suitable insertion point found
        spdlog::error("[PACMANCONFREPO] Could not find a suitable insertion point in {}", file_path);
        return false;
    }

    auto line = *insertion_point_it;
    auto pos  = file_content.find(line);
    file_content.insert(pos - 1, std::string{'\n'} + std::string{value} + std::string{'\n'});

    std::ofstream pacmanconf_file{file_path.data()};
    if (!pacmanconf_file.is_open()) {
        spdlog::error("[PACMANCONFREPO] '{}' open failed: {}", file_path, std::strerror(errno));
        return false;
    }
    pacmanconf_file << file_content;
    return true;
}

auto get_repo_list(std::string_view file_path) noexcept -> std::vector<std::string> {
    auto&& file_content = file_utils::read_whole_file(file_path);
    if (file_content.empty()) {
        spdlog::error("[PACMANCONFREPO] '{}' error occurred!", file_path);
        return {};
    }
    return file_content | ranges::views::split('\n')
        | ranges::views::filter([](auto&& rng) {
              auto&& line = std::string_view(&*rng.begin(), static_cast<size_t>(ranges::distance(rng)));
              return !line.empty() && !line.starts_with('#') && line.starts_with('[') && !line.starts_with("[options]");
          })
        | ranges::to<std::vector<std::string>>();
}

}  // namespace gucc::detail::pacmanconf
