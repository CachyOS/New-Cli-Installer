#include "gucc/pacmanconf_repo.hpp"
#include "gucc/conf.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/ranges_utils.hpp"
#include "gucc/string_utils.hpp"

#include <ranges>  // for ranges::*
#include <string>  // for string
#include <vector>  // for vector

#include <fmt/compile.h>
#include <fmt/format.h>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

// any section but [options] is a repo
constexpr auto repo_name(std::string_view line) noexcept -> std::optional<std::string_view> {
    return gucc::conf::section_name(line).and_then([](std::string_view name) {
        return name == "options"sv ? std::nullopt : std::optional{name};
    });
}

constexpr auto is_repo_header(std::string_view line) noexcept -> bool {
    return repo_name(line).has_value();
}

}  // namespace

namespace gucc::detail::pacmanconf {

auto repo_headers(std::string_view content) noexcept -> std::vector<std::string> {
    return utils::make_split_view(content)
        | std::ranges::views::transform(repo_name)
        | std::ranges::views::join
        | std::ranges::views::transform([](std::string_view name) { return fmt::format(FMT_COMPILE("[{}]"), name); })
        | std::ranges::to<std::vector<std::string>>();
}

auto insert_before_first_repo(std::string_view content, std::string_view value) noexcept -> std::optional<std::string> {
    const auto first_repo = utils::find_if_optional(utils::make_split_view(content), is_repo_header);
    if (!first_repo) {
        return std::nullopt;
    }
    // the line view points into content
    const auto pos = static_cast<std::size_t>(first_repo->data() - content.data());
    if (pos == 0) {
        return fmt::format(FMT_COMPILE("{}\n{}"), value, content);
    }
    return fmt::format(FMT_COMPILE("{}{}\n\n{}"), content.substr(0, pos), value, content.substr(pos));
}

auto push_repos_front(std::string_view file_path, std::string_view value) noexcept -> Result<void> {
    const auto& file_content = file_utils::read_whole_file(file_path);
    if (file_content.empty()) {
        return make_error(ErrorCode::FileIo, fmt::format(FMT_COMPILE("failed to read {}"), file_path));
    }
    const auto& rewritten = insert_before_first_repo(file_content, value);
    if (!rewritten) {
        return make_error(ErrorCode::ParseError, fmt::format(FMT_COMPILE("no repo section to insert before in {}"), file_path));
    }
    return file_utils::write_file(file_path, *rewritten);
}

auto get_repo_list(std::string_view file_path) noexcept -> std::vector<std::string> {
    const auto& file_content = file_utils::read_whole_file(file_path);
    if (file_content.empty()) {
        spdlog::error("[PACMANCONFREPO] '{}' error occurred!", file_path);
        return {};
    }
    return repo_headers(file_content);
}

}  // namespace gucc::detail::pacmanconf
