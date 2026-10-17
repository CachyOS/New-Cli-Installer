#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/pacmanconf_repo.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

static constexpr auto PACMANCONF_STR = R"(
[options]
HoldPkg     = pacman glibc
Architecture = auto

# Misc options
Color
CheckSpace
VerbosePkgLists
ParallelDownloads = 10

# Repository entries are of the format:
#       [repo-name]
#       Server = ServerName
#       Include = IncludePath
#
# The header [repo-name] is crucial - it must be present and
# uncommented to enable the repo.
#

# The testing repositories are disabled by default. To enable, uncomment the
# repo name header and Include lines. You can add preferred servers immediately
# after the header, and they will be used before the default mirrors.

#[testing]
#Include = /etc/pacman.d/mirrorlist

#[core-debug]
#Include = /etc/pacman.d/mirrorlist

#[extra-debug]
#Include = /etc/pacman.d/mirrorlist

[core]
Include = /etc/pacman.d/mirrorlist

[extra]
Include = /etc/pacman.d/mirrorlist

#[community-testing]
#Include = /etc/pacman.d/mirrorlist

[community]
Include = /etc/pacman.d/mirrorlist

#[multilib-testing]
#Include = /etc/pacman.d/mirrorlist

[multilib]
Include = /etc/pacman.d/mirrorlist
)";

static constexpr auto PACMANCONF_TEST = R"(
[options]
HoldPkg     = pacman glibc
Architecture = auto

# Misc options
Color
CheckSpace
VerbosePkgLists
ParallelDownloads = 10

# Repository entries are of the format:
#       [repo-name]
#       Server = ServerName
#       Include = IncludePath
#
# The header [repo-name] is crucial - it must be present and
# uncommented to enable the repo.
#

# The testing repositories are disabled by default. To enable, uncomment the
# repo name header and Include lines. You can add preferred servers immediately
# after the header, and they will be used before the default mirrors.

#[testing]
#Include = /etc/pacman.d/mirrorlist

#[core-debug]
#Include = /etc/pacman.d/mirrorlist

#[extra-debug]
#Include = /etc/pacman.d/mirrorlist

[cachyos]
Include = /etc/pacman.d/cachyos-mirrorlist

[core]
Include = /etc/pacman.d/mirrorlist

[extra]
Include = /etc/pacman.d/mirrorlist

#[community-testing]
#Include = /etc/pacman.d/mirrorlist

[community]
Include = /etc/pacman.d/mirrorlist

#[multilib-testing]
#Include = /etc/pacman.d/mirrorlist

[multilib]
Include = /etc/pacman.d/mirrorlist
)";

TEST_CASE("pacman conf test")
{
    using namespace gucc;

    static constexpr std::string_view filename{"/tmp/pacman.conf"};

    SECTION("get current repos")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, PACMANCONF_STR));

        auto repo_list = detail::pacmanconf::get_repo_list(filename);
        REQUIRE(!repo_list.empty());
        REQUIRE((repo_list == std::vector<std::string>{"[core]", "[extra]", "[community]", "[multilib]"}));

        // Cleanup.
        fs::remove(filename);
    }
    SECTION("push repo")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, PACMANCONF_STR));

        REQUIRE(detail::pacmanconf::push_repos_front(filename, "[cachyos]\nInclude = /etc/pacman.d/cachyos-mirrorlist"));

        // check repo list after pushing repo
        auto repo_list = detail::pacmanconf::get_repo_list(filename);
        REQUIRE(!repo_list.empty());
        REQUIRE((repo_list == std::vector<std::string>{"[cachyos]", "[core]", "[extra]", "[community]", "[multilib]"}));

        // check if file is equal to test data
        const auto& file_content = file_utils::read_whole_file(filename);
        REQUIRE(file_content == PACMANCONF_TEST);

        // Cleanup.
        fs::remove(filename);
    }
    SECTION("push repo before a header on the first line")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, "[core]\nInclude = /etc/pacman.d/mirrorlist\n"));

        REQUIRE(detail::pacmanconf::push_repos_front(filename, "[cachyos]"));
        REQUIRE_EQ(file_utils::read_whole_file(filename), "[cachyos]\n[core]\nInclude = /etc/pacman.d/mirrorlist\n");

        fs::remove(filename);
    }
    SECTION("header text repeated in a comment above it")
    {
        REQUIRE(file_utils::create_file_for_overwrite(filename, "#[core]\n\n[core]\n"));

        REQUIRE(detail::pacmanconf::push_repos_front(filename, "[cachyos]"));
        REQUIRE_EQ(file_utils::read_whole_file(filename), "#[core]\n\n[cachyos]\n\n[core]\n");

        fs::remove(filename);
    }
}

TEST_CASE("pacman conf pure transforms")
{
    using gucc::detail::pacmanconf::insert_before_first_repo;
    using gucc::detail::pacmanconf::repo_headers;
    using strings = std::vector<std::string>;

    SECTION("repo_headers on a stock pacman.conf")
    {
        REQUIRE_EQ(repo_headers(PACMANCONF_STR), strings{"[core]", "[extra]", "[community]", "[multilib]"});
    }
    SECTION("repo_headers skips [options], comments and malformed headers")
    {
        REQUIRE(repo_headers("").empty());
        REQUIRE(repo_headers("[options]\nColor\n#[core]\n[]\n[broken\n").empty());
        REQUIRE_EQ(repo_headers("  [core]  # main repo\n\t[extra]\n"), strings{"[core]", "[extra]"});
    }
    SECTION("repo_headers with CRLF line endings")
    {
        REQUIRE_EQ(repo_headers("[options]\r\nColor\r\n[core]\r\nInclude = x\r\n[extra]\r\n"), strings{"[core]", "[extra]"});
    }
    SECTION("insert_before_first_repo on stock pacman.conf")
    {
        REQUIRE_EQ(insert_before_first_repo(PACMANCONF_STR, "[cachyos]\nInclude = /etc/pacman.d/cachyos-mirrorlist"), std::optional<std::string>{PACMANCONF_TEST});
    }
    SECTION("insert_before_first_repo when the header opens the file")
    {
        REQUIRE_EQ(insert_before_first_repo("[core]\n", "[cachyos]"), std::optional<std::string>{"[cachyos]\n[core]\n"});
    }
    SECTION("insert_before_first_repo skips [options] and commented headers")
    {
        REQUIRE_EQ(insert_before_first_repo("[options]\nColor\n#[core]\n[core]\n", "[cachyos]"),
            std::optional<std::string>{"[options]\nColor\n#[core]\n[cachyos]\n\n[core]\n"});
    }
    SECTION("insert_before_first_repo keeps the header's indentation and CRLF")
    {
        REQUIRE_EQ(insert_before_first_repo("[options]\r\n  [core]\r\n", "[cachyos]"),
            std::optional<std::string>{"[options]\r\n[cachyos]\n\n  [core]\r\n"});
    }
    SECTION("insert_before_first_repo refuses when there is no repo")
    {
        REQUIRE_FALSE(insert_before_first_repo("", "[cachyos]"));
        REQUIRE_FALSE(insert_before_first_repo("[options]\nColor\n#[core]\n", "[cachyos]"));
    }
}
