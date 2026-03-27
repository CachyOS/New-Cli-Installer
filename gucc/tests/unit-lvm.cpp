#include "doctest_compatibility.h"

#include "gucc/logger.hpp"
#include "gucc/lvm.hpp"

#include <string_view>

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

TEST_CASE("lvm test")
{
    auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>([](const spdlog::details::log_msg&) {
        // noop
    });
    auto logger        = std::make_shared<spdlog::logger>("default", callback_sink);
    spdlog::set_default_logger(logger);
    gucc::logger::set_logger(logger);

    SECTION("detect lvm returns valid structure")
    {
        auto info = gucc::lvm::detect_lvm();
        if (info.physical_volumes.empty() || info.volume_groups.empty() || info.logical_volumes.empty()) {
            REQUIRE_FALSE(info.is_active());
        } else {
            REQUIRE(info.is_active());
        }
    }

    SECTION("lvm info is_active logic")
    {
        gucc::lvm::LvmInfo empty_info{};
        REQUIRE_FALSE(empty_info.is_active());

        gucc::lvm::LvmInfo partial_info{
            .physical_volumes = {"pv1"},
            .volume_groups = {},
            .logical_volumes = {},
        };
        REQUIRE_FALSE(partial_info.is_active());

        gucc::lvm::LvmInfo full_info{
            .physical_volumes = {"pv1"},
            .volume_groups = {"vg1"},
            .logical_volumes = {"lv1"},
        };
        REQUIRE(full_info.is_active());
    }
}

TEST_CASE("parse_vgs_output test")
{
    using gucc::lvm::parse_vgs_output;

    SECTION("empty input")
    {
        auto result = parse_vgs_output(""sv);
        CHECK(result.empty());
    }

    SECTION("single volume group")
    {
        constexpr auto input = "  vg0\t100.00g\n"sv;
        auto result = parse_vgs_output(input);
        REQUIRE(result.size() == 1);
        CHECK(result[0].first == "vg0");
        CHECK(result[0].second == "100.00g");
    }

    SECTION("multiple volume groups")
    {
        constexpr auto input = "  vg0\t100.00g\n  vg1\t500.00g\n  vg_data\t1000.00g\n"sv;
        auto result = parse_vgs_output(input);
        REQUIRE(result.size() == 3);
        CHECK(result[0].first == "vg0");
        CHECK(result[0].second == "100.00g");
        CHECK(result[1].first == "vg1");
        CHECK(result[1].second == "500.00g");
        CHECK(result[2].first == "vg_data");
        CHECK(result[2].second == "1000.00g");
    }

    SECTION("handles whitespace")
    {
        constexpr auto input = "  vg0  \t  50.00g  \n"sv;
        auto result = parse_vgs_output(input);
        REQUIRE(result.size() == 1);
        CHECK(result[0].first == "vg0");
        CHECK(result[0].second == "50.00g");
    }

    SECTION("filters empty lines")
    {
        constexpr auto input = "\n  vg0\t100.00g\n\n  vg1\t200.00g\n\n"sv;
        auto result = parse_vgs_output(input);
        REQUIRE(result.size() == 2);
        CHECK(result[0].first == "vg0");
        CHECK(result[1].first == "vg1");
    }

    SECTION("typical vgs output")
    {
        // Output from: vgs -o vg_name,vg_size --noheading --separator='\t' --units=g
        constexpr auto input = "  cachyos-vg\t476.45g\n"sv;
        auto result = parse_vgs_output(input);
        REQUIRE(result.size() == 1);
        CHECK(result[0].first == "cachyos-vg");
        CHECK(result[0].second == "476.45g");
    }
}
