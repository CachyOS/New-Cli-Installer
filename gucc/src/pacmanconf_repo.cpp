#include "gucc/pacmanconf_repo.hpp"
#include "gucc/file_utils.hpp"

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

#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/split.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace gucc::detail::pacmanconf {

bool push_repos_front(std::string_view file_path, std::string_view value) noexcept {
    using StringVec     = std::vector<std::string>;
    auto&& file_content = file_utils::read_whole_file(file_path);
    if (file_content.empty()) {
        spdlog::error("[PACMANCONFREPO] '{}' error occurred!", file_path);
        return false;
    }
    auto&& content_lines = file_content | ranges::views::split('\n') | ranges::to<std::vector<std::string>>();
    for (std::size_t i = 1; i < content_lines.size(); ++i) {
        const std::string_view current_line{content_lines[i - 1]};
        if (current_line.empty() || current_line.starts_with('#') || !current_line.starts_with('[') || current_line.starts_with("[options]")) {
            continue;
        }

        auto&& to_be_inserted = value | ranges::views::split('\n') | ranges::to<std::vector<std::string>>();
        to_be_inserted.emplace_back("\n");
        content_lines.insert(content_lines.begin() + static_cast<StringVec::iterator::difference_type>(i - 1),
            std::move_iterator(to_be_inserted.begin()), std::move_iterator(to_be_inserted.end()));
        break;
    }

    std::ofstream pacmanconf_file{file_path.data()};
    if (!pacmanconf_file.is_open()) {
        spdlog::error("[PACMANCONFREPO] '{}' open failed: {}", file_path, std::strerror(errno));
        return false;
    }
    std::string&& result = content_lines | ranges::views::join('\n') | ranges::to<std::string>();
    pacmanconf_file << result;
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
