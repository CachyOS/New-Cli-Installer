#include "gucc/os_prober.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/string_utils.hpp"

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace gucc::os_prober::detail {

auto parse(std::string_view output) noexcept -> std::vector<Entry> {
    std::vector<Entry> entries;
    for (const auto line : utils::nonblank_lines(output)) {
        const auto fields = utils::split_fields<3>(line, ':');
        if (!fields) {
            continue;
        }
        const auto& [path, long_name, short_name] = *fields;
        if (!path.starts_with("/dev/"sv)) {
            continue;
        }
        const auto loader = utils::split_once(path, '@');

        // a blank long name falls back to the short one
        entries.emplace_back(Entry{
            .device      = std::string{loader ? loader->first : path},
            .efi_path    = loader.transform([](auto split) { return std::string{split.second}; }),
            .pretty_name = std::string{utils::trim(long_name).empty() ? utils::trim(short_name) : utils::trim(long_name)},
        });
    }
    return entries;
}

}  // namespace gucc::os_prober::detail

namespace gucc::os_prober {

auto probe() noexcept -> std::vector<Entry> {
    return utils::query_output(std::vector{"/usr/bin/os-prober"s})
        .transform(detail::parse)
        .value_or(std::vector<Entry>{});
}

}  // namespace gucc::os_prober
