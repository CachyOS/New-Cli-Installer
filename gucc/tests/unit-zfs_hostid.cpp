#include "doctest_compatibility.h"
#include "test_temp_root.hpp"

#include "gucc/zfs.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace {
using gucc::tests::TempRoot;

[[nodiscard]] auto read_file(const fs::path& file) -> std::string {
    std::ifstream in{file, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, {}};
}
}

TEST_CASE("copy_hostid_to_target")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);

    SECTION("basic")
    {
        TempRoot src_dir;
        TempRoot root;

        const auto host_hostid = src_dir.path() / "hostid";
        const std::string payload{"\x01\x00\xab\xcd", 4};
        std::ofstream{host_hostid, std::ios::binary} << payload;

        REQUIRE(gucc::fs::copy_hostid_to_target(root.path().string(), host_hostid.string()));

        const auto dest = root.path() / "etc" / "hostid";
        REQUIRE(fs::exists(dest));
        CHECK(read_file(dest) == payload);
    }
    SECTION("precopy exist")
    {
        TempRoot src_dir;
        TempRoot root;

        const auto host_hostid = src_dir.path() / "hostid";
        std::ofstream{host_hostid, std::ios::binary} << "abcd";

        REQUIRE_FALSE(fs::exists(root.path() / "etc"));
        REQUIRE(gucc::fs::copy_hostid_to_target(root.path().string(), host_hostid.string()));
        CHECK(fs::exists(root.path() / "etc" / "hostid"));
    }
    SECTION("overwrite")
    {
        TempRoot src_dir;
        TempRoot root;

        const auto host_hostid = src_dir.path() / "hostid";
        std::ofstream{host_hostid, std::ios::binary} << "newvalue";

        fs::create_directories(root.path() / "etc");
        std::ofstream{root.path() / "etc" / "hostid", std::ios::binary} << "stale";

        REQUIRE(gucc::fs::copy_hostid_to_target(root.path().string(), host_hostid.string()));
        CHECK(read_file(root.path() / "etc" / "hostid") == "newvalue");
    }
    SECTION("missing")
    {
        TempRoot root;
        const auto absent = root.path() / "does-not-exist";

        REQUIRE(gucc::fs::copy_hostid_to_target(root.path().string(), absent.string()));
        CHECK_FALSE(fs::exists(root.path() / "etc" / "hostid"));
    }
}
