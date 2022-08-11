#include "initcpio.hpp"
#include "utils.hpp"

#include <fstream>  // for ofstream

#include <fmt/core.h>
#include <fmt/format.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <range/v3/algorithm/find.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/getlines.hpp>
#include <range/v3/view/join.hpp>
#include <range/v3/view/split.hpp>
#include <range/v3/view/transform.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace detail {

bool Initcpio::write() const noexcept {
    auto&& file_content  = utils::read_whole_file(m_file_path);
    std::string&& result = file_content | ranges::views::split('\n')
        | ranges::views::transform([&](auto&& rng) {
              auto&& line = std::string_view(&*rng.begin(), static_cast<size_t>(ranges::distance(rng)));
              if (line.starts_with("MODULES")) {
                  auto&& formatted_modules = modules | ranges::views::join(' ')
                                                     | ranges::to<std::string>();
                  return fmt::format("MODULES=({})", formatted_modules);
              } else if (line.starts_with("FILES")) {
                  auto&& formatted_files = files | ranges::views::join(' ')
                                                 | ranges::to<std::string>();
                  return fmt::format("FILES=({})", formatted_files);
              } else if (line.starts_with("HOOKS")) {
                  auto&& formatted_hooks = hooks | ranges::views::join(' ')
                                                 | ranges::to<std::string>();
                  return fmt::format("HOOKS=({})", formatted_hooks);
              }
              return std::string{line.data(), line.size()};
          })
        | ranges::views::join('\n')
        | ranges::to<std::string>();
    result += '\n';

    /* clang-format off */
    std::ofstream mhinitcpio_file{m_file_path.data()};
    if (!mhinitcpio_file.is_open()) { return false; }
    /* clang-format on */
    mhinitcpio_file << result;
    return true;
}

bool Initcpio::parse_file() noexcept {
    auto&& file_content  = utils::read_whole_file(m_file_path);

    const auto& parse_line = [](auto&& line) -> std::vector<std::string> {
        auto&& open_bracket_pos = line.find('(');
        auto&& close_bracket = ranges::find(line, ')');
        if (open_bracket_pos != std::string::npos && close_bracket != line.end()) {
            const auto length = ranges::distance(line.begin() + static_cast<long>(open_bracket_pos), close_bracket - 1);

            auto&& input_data = line.substr(open_bracket_pos + 1, static_cast<std::size_t>(length));
            return input_data | ranges::views::split(' ') | ranges::to<std::vector<std::string>>();
        }
        return {};
    };

    auto&& file_content_lines = utils::make_multiline(file_content);
    for (auto&& line : file_content_lines) {
        if (line.starts_with("MODULES")) {
            modules = parse_line(line);
        } else if (line.starts_with("FILES")) {
            files = parse_line(line);
        } else if (line.starts_with("HOOKS")) {
            hooks = parse_line(line);
        }
    }

    return true;
}

}  // namespace detail
