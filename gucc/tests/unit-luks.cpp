#include "doctest_compatibility.h"

#include "gucc/luks.hpp"

#include <string>
#include <string_view>

using namespace std::string_view_literals;

TEST_CASE("LuksVersion enum test")
{
    SECTION("enum values")
    {
        REQUIRE_EQ(static_cast<int>(gucc::crypto::LuksVersion::Luks1), 1);
        REQUIRE_EQ(static_cast<int>(gucc::crypto::LuksVersion::Luks2), 2);
    }
}

TEST_CASE("Tpm2Config defaults test")
{
    SECTION("default pcrs")
    {
        gucc::crypto::Tpm2Config config{};
        REQUIRE_EQ(config.pcrs, "0,2,4,7");
    }
    SECTION("default device")
    {
        gucc::crypto::Tpm2Config config{};
        REQUIRE_EQ(config.device, "auto");
    }
    SECTION("custom pcrs")
    {
        gucc::crypto::Tpm2Config config{.pcrs = "0,7,8,9"};
        REQUIRE_EQ(config.pcrs, "0,7,8,9");
        REQUIRE_EQ(config.device, "auto");
    }
    SECTION("custom device")
    {
        gucc::crypto::Tpm2Config config{.device = "/dev/tpmrm0"};
        REQUIRE_EQ(config.pcrs, "0,2,4,7");
        REQUIRE_EQ(config.device, "/dev/tpmrm0");
    }
    SECTION("both custom")
    {
        gucc::crypto::Tpm2Config config{.pcrs = "7", .device = "/dev/tpm0"};
        REQUIRE_EQ(config.pcrs, "7");
        REQUIRE_EQ(config.device, "/dev/tpm0");
    }
}
