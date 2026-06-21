#include "doctest_compatibility.h"

#include "gucc/error.hpp"

#include <string>

TEST_CASE("error type test")
{
    SECTION("make_error carries code and context")
    {
        const gucc::Result<void> result = gucc::make_error(gucc::ErrorCode::FileIo, "boom");
        REQUIRE(!result.has_value());
        REQUIRE_EQ(result.error().code, gucc::ErrorCode::FileIo);
        REQUIRE_EQ(result.error().context, std::string{"boom"});
    }

    SECTION("to_string renders <code>: <context>")
    {
        const gucc::Error err{.code = gucc::ErrorCode::SubprocessFailed, .context = "cmd failed"};
        REQUIRE_EQ(gucc::to_string(err), std::string{"SubprocessFailed: cmd failed"});
    }

    SECTION("default Error is Unknown")
    {
        const gucc::Error err{};
        REQUIRE_EQ(err.code, gucc::ErrorCode::Unknown);
        REQUIRE_EQ(gucc::to_string(err), std::string{"Unknown: "});
    }

    SECTION("Result<T> value path")
    {
        const gucc::Result<int> ok = 42;
        REQUIRE(ok.has_value());
        REQUIRE_EQ(*ok, 42);
    }
}
