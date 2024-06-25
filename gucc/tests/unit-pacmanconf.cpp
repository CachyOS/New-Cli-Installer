#include "file_utils.hpp"
#include "pacmanconf_repo.hpp"

#include <cassert>
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
Include = /etc/pacman.d/mirrorlist)";

int main() {
    using namespace gucc;

    static constexpr std::string_view filename{"/tmp/pacman.conf"};

    // Open pacmanconf file for writing.
    std::ofstream pacmanconf_file{filename.data()};
    assert(pacmanconf_file.is_open());

    // Setup pacmanconf file.
    pacmanconf_file << PACMANCONF_STR;
    pacmanconf_file.close();

    // Get current repos.
    auto repo_list = detail::pacmanconf::get_repo_list(filename);
    assert(!repo_list.empty());
    assert((repo_list == std::vector<std::string>{"[core]", "[extra]", "[community]", "[multilib]"}));

    // Push repo.
    assert(detail::pacmanconf::push_repos_front(filename, "[cachyos]\nInclude = /etc/pacman.d/cachyos-mirrorlist"));

    // Check repo list after pushing repo.
    repo_list = detail::pacmanconf::get_repo_list(filename);
    assert(!repo_list.empty());
    assert((repo_list == std::vector<std::string>{"[cachyos]", "[core]", "[extra]", "[community]", "[multilib]"}));

    // Check if file is equal to test data.
    const auto& file_content = file_utils::read_whole_file(filename);
    assert(file_content == PACMANCONF_TEST);

    // Cleanup.
    fs::remove(filename);
}
