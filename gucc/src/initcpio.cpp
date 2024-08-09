#include "gucc/initcpio.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

#include <fstream>  // for ofstream

#include <fmt/compile.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

constexpr auto modify_initcpio_line(std::string_view line, std::string_view modules, std::string_view files, std::string_view hooks) noexcept -> std::string {
    if (line.starts_with("MODULES"sv)) {
        return fmt::format(FMT_COMPILE("MODULES=({})"), modules);
    } else if (line.starts_with("FILES"sv)) {
        return fmt::format(FMT_COMPILE("FILES=({})"), files);
    } else if (line.starts_with("HOOKS"sv)) {
        return fmt::format(FMT_COMPILE("HOOKS=({})"), hooks);
    }
    return std::string{line.data(), line.size()};
}

constexpr auto modify_initcpio_fields(std::string_view file_content, std::string_view modules, std::string_view files, std::string_view hooks) noexcept -> std::string {
    return file_content | std::ranges::views::split('\n')
        | std::ranges::views::transform([&](auto&& rng) {
              /* clang-format off */
              auto&& line = std::string_view(&*rng.begin(), static_cast<size_t>(std::ranges::distance(rng)));
              return modify_initcpio_line(line, modules, files, hooks);
          })
        | std::ranges::views::join_with('\n')
        | std::ranges::to<std::string>();
}

}  // namespace

namespace gucc::detail {

bool Initcpio::write() const noexcept {
    auto&& file_content = file_utils::read_whole_file(m_file_path);
    if (file_content.empty()) {
        spdlog::error("[INITCPIO] '{}' error occurred!", m_file_path);
        return false;
    }
    const auto& formatted_modules = utils::join(modules, ' ');
    const auto& formatted_files = utils::join(files, ' ');
    const auto& formatted_hooks = utils::join(hooks, ' ');

    auto result = modify_initcpio_fields(file_content, formatted_modules, formatted_files, formatted_hooks);
    return file_utils::create_file_for_overwrite(m_file_path, result);
}

bool Initcpio::parse_file() noexcept {
    auto&& file_content = file_utils::read_whole_file(m_file_path);
    if (file_content.empty()) {
        spdlog::error("[INITCPIO] '{}' error occurred!", m_file_path);
        return false;
    }

    const auto& parse_line = [](std::string_view line) -> std::vector<std::string> {
        auto&& open_bracket_pos = line.find('(');
        auto&& close_bracket    = std::ranges::find(line, ')');
        if (open_bracket_pos != std::string_view::npos && close_bracket != line.end()) {
            const auto length = std::ranges::distance(line.begin() + static_cast<std::int64_t>(open_bracket_pos), close_bracket - 1);

            auto&& input_data = line.substr(open_bracket_pos + 1, static_cast<std::size_t>(length));
            return input_data | std::ranges::views::split(' ') | std::ranges::to<std::vector<std::string>>();
        }
        return {};
    };

    auto&& file_content_lines = utils::make_split_view(file_content);
    for (auto&& line : file_content_lines) {
        if (line.starts_with("MODULES"sv)) {
            modules = parse_line(std::move(line));
        } else if (line.starts_with("FILES"sv)) {
            files = parse_line(std::move(line));
        } else if (line.starts_with("HOOKS"sv)) {
            hooks = parse_line(std::move(line));
        }
    }

    return true;
}

}  // namespace gucc::detail
