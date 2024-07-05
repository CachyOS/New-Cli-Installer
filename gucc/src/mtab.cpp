#include "gucc/mtab.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

using namespace std::string_view_literals;

namespace gucc::mtab {

auto parse_mtab_content(std::string_view mtab_content, std::string_view root_mountpoint) noexcept -> std::vector<MTabEntry> {
    std::vector<MTabEntry> entries{};

    auto&& file_content_lines = utils::make_split_view(mtab_content);
    for (auto&& line : file_content_lines) {
        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        auto&& line_split = utils::make_split_view(line, ' ');
        auto&& device     = *utils::index_viewable_range(line_split, 0);
        if (utils::size_viewable_range(line_split) >= 3 && !device.starts_with('#')) {
            // e.g format: <device> <mountpoint> <fstype> <options>
            auto&& mountpoint = *utils::index_viewable_range(line_split, 1);
            if (mountpoint.starts_with(root_mountpoint)) {
                entries.emplace_back(MTabEntry{.device = std::string{device}, .mountpoint = std::string{mountpoint}});
            }
        }
    }
    return entries;
}

auto parse_mtab(std::string_view root_mountpoint, std::string_view mtab_path) noexcept -> std::optional<std::vector<MTabEntry>> {
    auto&& file_content = file_utils::read_whole_file(mtab_path);
    if (file_content.empty()) {
        return std::nullopt;
    }
    return mtab::parse_mtab_content(file_content, root_mountpoint);
}

}  // namespace gucc::mtab
