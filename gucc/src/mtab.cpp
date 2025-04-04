#include "gucc/mtab.hpp"
#include "gucc/file_utils.hpp"
#include "gucc/string_utils.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;
namespace fs = std::filesystem;

namespace {

auto read_mtab(std::string_view mtab_path) noexcept -> std::optional<std::string> {
    // mtab file size is reported as zero bytes
    // so just read line by line until EOF
    std::ifstream file(fs::path{mtab_path}, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::vector<std::string> lines{};
    while (file) {
        std::string line{};
        std::getline(file, line);
        lines.push_back(std::move(line));
    }

    auto&& mtab_content = gucc::utils::join(lines);
    return std::make_optional<std::string>(std::move(mtab_content));
}

}  // namespace

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
    // mountpoint and mtab paths cannot be empty
    if (root_mountpoint.empty() || mtab_path.empty()) {
        return std::nullopt;
    }

    // use "non-standard" read due to reported zero size
    auto&& file_content = read_mtab(mtab_path);
    if (!file_content || file_content->empty()) {
        return std::nullopt;
    }
    return mtab::parse_mtab_content(*file_content, root_mountpoint);
}

}  // namespace gucc::mtab
