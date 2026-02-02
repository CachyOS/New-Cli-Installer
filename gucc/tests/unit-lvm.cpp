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
