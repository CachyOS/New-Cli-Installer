#include "doctest_compatibility.h"

#include "gucc/machine_id.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <system_error>

#include <fmt/format.h>

namespace fs = std::filesystem;

namespace {

class TempRoot final {
 public:
    TempRoot()
      : m_path(fs::temp_directory_path() / fs::path{fmt::format("gucc-machine-id-{}", std::random_device{}())}) {
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

[[nodiscard]] auto looks_like_machine_id(const fs::path& file) -> bool {
    std::ifstream in{file};
    std::string contents(std::istreambuf_iterator<char>{in}, {});
    if (!contents.empty() && contents.back() == '\n') {
        contents.pop_back();
    }
    if (contents.size() != 32) {
        return false;
    }
    return std::ranges::all_of(contents, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

[[nodiscard]] auto systemd_machine_id_setup_available() -> bool {
    static const bool available = [] {
        std::error_code ec;
        for (const auto& dir : {"/usr/bin", "/usr/sbin", "/bin", "/sbin"}) {
            if (fs::exists(fs::path{dir} / "systemd-machine-id-setup", ec)) {
                return true;
            }
        }
        return false;
    }();
    return available;
}

}  // namespace

TEST_CASE("machine_id::detail::clear_existing")
{
    SECTION("removes /etc/machine-id and dbus path if present")
    {
        TempRoot root;
        fs::create_directories(root.path() / "etc");
        fs::create_directories(root.path() / "var" / "lib" / "dbus");
        std::ofstream{root.path() / "etc" / "machine-id"} << "deadbeef\n";
        std::ofstream{root.path() / "var" / "lib" / "dbus" / "machine-id"} << "stale\n";

        REQUIRE(gucc::machine_id::detail::clear_existing(root.path().string()));

        CHECK_FALSE(fs::exists(root.path() / "etc" / "machine-id"));
        CHECK_FALSE(fs::exists(root.path() / "var" / "lib" / "dbus" / "machine-id"));
    }
    SECTION("succeeds when nothing is there yet")
    {
        TempRoot root;
        REQUIRE(gucc::machine_id::detail::clear_existing(root.path().string()));
    }
}

TEST_CASE("machine_id::detail::link_dbus_to_etc")
{
    SECTION("creates the parent directory and the symlink")
    {
        TempRoot root;
        REQUIRE(gucc::machine_id::detail::link_dbus_to_etc(root.path().string()));

        const auto dbus = root.path() / "var" / "lib" / "dbus" / "machine-id";
        REQUIRE(fs::is_symlink(dbus));
        CHECK(fs::read_symlink(dbus) == fs::path{"/etc/machine-id"});
    }
    SECTION("replaces a stale regular file with the symlink")
    {
        TempRoot root;
        fs::create_directories(root.path() / "var" / "lib" / "dbus");
        std::ofstream{root.path() / "var" / "lib" / "dbus" / "machine-id"} << "stale\n";

        REQUIRE(gucc::machine_id::detail::link_dbus_to_etc(root.path().string()));

        const auto dbus = root.path() / "var" / "lib" / "dbus" / "machine-id";
        REQUIRE(fs::is_symlink(dbus));
        CHECK(fs::read_symlink(dbus) == fs::path{"/etc/machine-id"});
    }
    SECTION("is idempotent")
    {
        TempRoot root;
        REQUIRE(gucc::machine_id::detail::link_dbus_to_etc(root.path().string()));
        REQUIRE(gucc::machine_id::detail::link_dbus_to_etc(root.path().string()));

        const auto dbus = root.path() / "var" / "lib" / "dbus" / "machine-id";
        REQUIRE(fs::is_symlink(dbus));
    }
}

TEST_CASE("machine_id::reset" * doctest::skip(!systemd_machine_id_setup_available()))
{
    SECTION("generates a 32-hex machine-id and links the dbus path")
    {
        TempRoot root;
        REQUIRE(gucc::machine_id::reset(root.path().string()));

        const auto etc  = root.path() / "etc" / "machine-id";
        const auto dbus = root.path() / "var" / "lib" / "dbus" / "machine-id";

        REQUIRE(fs::is_regular_file(etc));
        CHECK(looks_like_machine_id(etc));
        REQUIRE(fs::is_symlink(dbus));
        CHECK(fs::read_symlink(dbus) == fs::path{"/etc/machine-id"});
    }
    SECTION("replaces an inherited live-ISO machine-id")
    {
        TempRoot root;
        fs::create_directories(root.path() / "etc");
        const auto etc = root.path() / "etc" / "machine-id";
        std::ofstream{etc} << "deadbeefdeadbeefdeadbeefdeadbeef\n";

        REQUIRE(gucc::machine_id::reset(root.path().string()));
        REQUIRE(fs::is_regular_file(etc));
        CHECK(looks_like_machine_id(etc));
    }
}
