#include "doctest_compatibility.h"

#include "cachyos/disk_validation.hpp"
#include "cachyos/installer_config.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

using cachyos::installer::parse_size_bytes;
using cachyos::installer::recommend_swap_size;
using cachyos::installer::validate_disk_space;
using cachyos::installer::InstallerConfig;

static auto setup_test_logger() noexcept -> void {
    auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {});
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("test", sink));
}

TEST_CASE("size parsing — binary suffixes")
{
    REQUIRE_EQ(parse_size_bytes("1KiB"), uint64_t{1024});
    REQUIRE_EQ(parse_size_bytes("512MiB"), uint64_t{512ULL * 1024 * 1024});
    REQUIRE_EQ(parse_size_bytes("4GiB"), uint64_t{4ULL * 1024 * 1024 * 1024});
    REQUIRE_EQ(parse_size_bytes("1TiB"), uint64_t{1ULL * 1024 * 1024 * 1024 * 1024});
}

TEST_CASE("size parsing — decimal suffixes")
{
    REQUIRE_EQ(parse_size_bytes("1KB"), uint64_t{1000});
    REQUIRE_EQ(parse_size_bytes("500MB"), uint64_t{500ULL * 1000 * 1000});
    REQUIRE_EQ(parse_size_bytes("2GB"), uint64_t{2ULL * 1000 * 1000 * 1000});
}

TEST_CASE("size parsing — legacy single-letter suffixes")
{
    REQUIRE_EQ(parse_size_bytes("512M"), uint64_t{512ULL * 1024 * 1024});
    REQUIRE_EQ(parse_size_bytes("4G"), uint64_t{4ULL * 1024 * 1024 * 1024});
    REQUIRE_EQ(parse_size_bytes("1T"), uint64_t{1ULL * 1024 * 1024 * 1024 * 1024});
}

TEST_CASE("size parsing — edge cases")
{
    REQUIRE(!parse_size_bytes("").has_value());
    REQUIRE(!parse_size_bytes("100%").has_value());
    REQUIRE(!parse_size_bytes("invalid").has_value());
    REQUIRE(!parse_size_bytes("  ").has_value());
}

TEST_CASE("size parsing — raw numbers (bytes)")
{
    REQUIRE_EQ(parse_size_bytes("1048576"), uint64_t{1048576});
    REQUIRE_EQ(parse_size_bytes("536870912"), uint64_t{536870912});
}

TEST_CASE("swap size recommendation — < 2 GiB RAM")
{
    constexpr uint64_t one_gib = uint64_t{1} << 30;
    REQUIRE_EQ(recommend_swap_size(1 * one_gib), uint64_t{2} * one_gib);
}

TEST_CASE("swap size recommendation — 2–<8 GiB RAM")
{
    constexpr uint64_t one_gib = uint64_t{1} << 30;
    REQUIRE_EQ(recommend_swap_size(2 * one_gib), 2 * one_gib);
    REQUIRE_EQ(recommend_swap_size(4 * one_gib), 4 * one_gib);
}

TEST_CASE("swap size recommendation — 8–<64 GiB RAM")
{
    constexpr uint64_t one_gib = uint64_t{1} << 30;
    REQUIRE_EQ(recommend_swap_size(8 * one_gib), 4 * one_gib);
    REQUIRE_EQ(recommend_swap_size(16 * one_gib), 8 * one_gib);
    REQUIRE_EQ(recommend_swap_size(32 * one_gib), 16 * one_gib);
}

TEST_CASE("swap size recommendation — >= 64 GiB RAM")
{
    constexpr uint64_t one_gib = uint64_t{1} << 30;
    REQUIRE_EQ(recommend_swap_size(64 * one_gib), 4 * one_gib);
    REQUIRE_EQ(recommend_swap_size(128 * one_gib), 4 * one_gib);
}

TEST_CASE("validate_disk_space — no device specified")
{
    setup_test_logger();
    InstallerConfig config{};
    config.headless_mode = true;
    // No device set — should fail with error about missing device

    const auto result = validate_disk_space(config, false);
    REQUIRE(!result);
}

TEST_CASE("validate_disk_space — nonexistent device")
{
    setup_test_logger();
    InstallerConfig config{};
    config.headless_mode = true;
    config.device = "/dev/nonexistent_disk_xyz";

    const auto result = validate_disk_space(config, false);
    REQUIRE(!result);
}
