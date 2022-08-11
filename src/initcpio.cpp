#include "initcpio.hpp"
#include "utils.hpp"

#include <fstream>  // for ofstream

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

}  // namespace detail
