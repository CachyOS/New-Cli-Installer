#include "doctest_compatibility.h"
#include "test_temp_root.hpp"

#include "gucc/plymouth.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <fmt/format.h>

namespace fs = std::filesystem;

namespace {

using gucc::tests::TempRoot;

void install_plymouth_binary(const fs::path& root) {
    const auto bin_dir = root / "usr" / "bin";
    fs::create_directories(bin_dir);
    std::ofstream{bin_dir / "plymouth"};
}

void install_theme(const fs::path& root, std::string_view name) {
    const auto theme_dir = root / "usr" / "share" / "plymouth" / "themes" / std::string{name};
    fs::create_directories(theme_dir);
    std::ofstream{theme_dir / fmt::format("{}.plymouth", name)};
}

}  // namespace

TEST_CASE("plymouth::is_installed")
{
    SECTION("missing binary returns false")
    {
        TempRoot root;
        REQUIRE_FALSE(gucc::plymouth::is_installed(root.path().string()));
    }
    SECTION("present binary returns true")
    {
        TempRoot root;
        install_plymouth_binary(root.path());
        REQUIRE(gucc::plymouth::is_installed(root.path().string()));
    }
}

TEST_CASE("plymouth::list_themes")
{
    SECTION("missing themes directory returns empty list")
    {
        TempRoot root;
        REQUIRE(gucc::plymouth::list_themes(root.path().string()).empty());
    }
    SECTION("subdirectory without descriptor is ignored")
    {
        TempRoot root;
        fs::create_directories(root.path() / "usr" / "share" / "plymouth" / "themes" / "broken");
        REQUIRE(gucc::plymouth::list_themes(root.path().string()).empty());
    }
    SECTION("returns sorted list of installed themes")
    {
        TempRoot root;
        install_theme(root.path(), "spinner");
        install_theme(root.path(), "cachyos-bootanimation");
        install_theme(root.path(), "details");
        const auto themes = gucc::plymouth::list_themes(root.path().string());
        REQUIRE_EQ(themes.size(), 3u);
        REQUIRE_EQ(themes[0], "cachyos-bootanimation");
        REQUIRE_EQ(themes[1], "details");
        REQUIRE_EQ(themes[2], "spinner");
    }
    SECTION("stray non-directory entries are ignored")
    {
        TempRoot root;
        const auto themes_dir = root.path() / "usr" / "share" / "plymouth" / "themes";
        fs::create_directories(themes_dir);
        std::ofstream{themes_dir / "README"};  // not a directory
        install_theme(root.path(), "spinner");
        const auto themes = gucc::plymouth::list_themes(root.path().string());
        REQUIRE_EQ(themes.size(), 1u);
        REQUIRE_EQ(themes[0], "spinner");
    }
}
