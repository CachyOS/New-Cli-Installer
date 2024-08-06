#include "doctest_compatibility.h"

#include "gucc/file_utils.hpp"
#include "gucc/pacmanconf_repo.hpp"

#include <filesystem>
#include <fstream>
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
}
