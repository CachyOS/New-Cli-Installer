#include "doctest_compatibility.h"

#include "gucc/fetch_file.hpp"
#include "gucc/file_utils.hpp"

#include <filesystem>
#include <string_view>

#include <fmt/format.h>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

TEST_CASE("fetch file test")
{
    static constexpr std::string_view LICENSE_PATH = GUCC_TOP_DIR "/LICENSE";

    SECTION("existing remote url, non existent fallback url")
    {
        static constexpr auto remote_url = "https://github.com/CachyOS/New-Cli-Installer/raw/master/LICENSE"sv;

        const auto& file_content = gucc::fetch::fetch_file_from_url(remote_url, "file:///ter-testunit");
        REQUIRE(file_content);
        REQUIRE(!file_content->empty());

        const auto& expected_file_content = gucc::file_utils::read_whole_file(LICENSE_PATH);
        REQUIRE_EQ(file_content, expected_file_content);
    }
    SECTION("non existent remote url, existing fallback url")
    {
        static constexpr auto remote_url = "https://github.com/CachyOS/New-Cli-Installer/raw/master/LCNS"sv;

        const auto& file_content = gucc::fetch::fetch_file_from_url(remote_url, fmt::format("file://{}", LICENSE_PATH));
        REQUIRE(file_content);
        REQUIRE(!file_content->empty());

        const auto& expected_file_content = gucc::file_utils::read_whole_file(LICENSE_PATH);
        REQUIRE_EQ(file_content, expected_file_content);
    }
    SECTION("non existent remote url, non existent fallback url")
    {
        static constexpr auto remote_url = "https://github.com/CachyOS/New-Cli-Installer/raw/master/LCNS"sv;

        const auto& file_content = gucc::fetch::fetch_file_from_url(remote_url, "file:///ter-testunit");
        REQUIRE(!file_content);
    }
}
