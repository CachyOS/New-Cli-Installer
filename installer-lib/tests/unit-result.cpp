#include "doctest_compatibility.h"

#include "cachyos/result.hpp"

#include <string_view>

using namespace std::string_view_literals;
using cachyos::installer::lift;

TEST_CASE("gucc results")
{
    SECTION("success passes the value through")
    {
        REQUIRE(lift(gucc::Result<void>{}));
        REQUIRE_EQ(lift(gucc::Result<int>{42}).value_or(0), 42);
    }
    SECTION("error keeps the context without the code")
    {
        const auto res = lift(gucc::Result<void>{gucc::make_error(gucc::ErrorCode::FileIo, "failed to write /etc/hosts")});
        REQUIRE_FALSE(res);
        REQUIRE_EQ(res.error(), "failed to write /etc/hosts"sv);
    }
    SECTION("prefix is prepended")
    {
        const auto res = lift(gucc::Result<int>{gucc::make_error(gucc::ErrorCode::SubprocessFailed, "exit 1")}, "zpool create"sv);
        REQUIRE_EQ(res.error(), "zpool create: exit 1"sv);
    }
}
