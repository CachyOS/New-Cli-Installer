#include "doctest_compatibility.h"

#include "gucc/plymouth.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

#include <fmt/format.h>

namespace fs = std::filesystem;

namespace {

class TempRoot final {
 public:
    TempRoot()
      : m_path(fs::temp_directory_path() / fs::path{fmt::format("gucc-plymouth-{}", std::random_device{}())}) {
        fs::create_directories(m_path);
    }
    ~TempRoot() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempRoot(const TempRoot&)                    = delete;
    auto operator=(const TempRoot&) -> TempRoot& = delete;
    TempRoot(TempRoot&&)                         = delete;
    auto operator=(TempRoot&&) -> TempRoot&      = delete;

    [[nodiscard]] auto path() const noexcept -> const fs::path& { return m_path; }

 private:
    fs::path m_path;
};

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
