#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest_compatibility.h"

#include "cli.hpp"

#include <string>

TEST_CASE("cli::parse")
{
    SECTION("--help returns nullopt")
    {
        const char* args[] = {"installer", "--help"};
        CHECK_FALSE(cli::parse(2, const_cast<char**>(args)).has_value());
    }

    SECTION("--version returns nullopt (argparse built-in)")
    {
        const char* args[] = {"installer", "--version"};
        // argparse prints version and throws an exception that we catch, returning nullopt
        CHECK_FALSE(cli::parse(2, const_cast<char**>(args)).has_value());
    }

    SECTION("unknown argument returns nullopt")
    {
        const char* args[] = {"installer", "--bogus"};
        CHECK_FALSE(cli::parse(2, const_cast<char**>(args)).has_value());
    }

    SECTION("defaults: config=settings.json, dry_run=false, verbose=false")
    {
        const char* args[] = {"installer"};
        auto result = cli::parse(1, const_cast<char**>(args));
        REQUIRE(result.has_value());
        CHECK_EQ(result->config_path, "settings.json");
        CHECK_FALSE(result->dry_run);
        CHECK_FALSE(result->verbose);
    }

    SECTION("--config sets config path")
    {
        const char* args[] = {"installer", "--config", "/path/to/config.json"};
        auto result = cli::parse(3, const_cast<char**>(args));
        REQUIRE(result.has_value());
        CHECK_EQ(result->config_path, "/path/to/config.json");
    }

    SECTION("--config=/path form")
    {
        const char* args[] = {"installer", "--config=/etc/install.json"};
        auto result = cli::parse(2, const_cast<char**>(args));
        REQUIRE(result.has_value());
        CHECK_EQ(result->config_path, "/etc/install.json");
    }

    SECTION("--dry-run sets dry_run flag")
    {
        const char* args[] = {"installer", "--dry-run"};
        auto result = cli::parse(2, const_cast<char**>(args));
        REQUIRE(result.has_value());
        CHECK(result->dry_run);
    }

    SECTION("--verbose sets verbose flag")
    {
        const char* args[] = {"installer", "--verbose"};
        auto result = cli::parse(2, const_cast<char**>(args));
        REQUIRE(result.has_value());
        CHECK(result->verbose);
    }

    SECTION("multiple flags combined")
    {
        const char* args[] = {"installer", "--config", "custom.json", "--dry-run", "--verbose"};
        auto result = cli::parse(5, const_cast<char**>(args));
        REQUIRE(result.has_value());
        CHECK_EQ(result->config_path, "custom.json");
        CHECK(result->dry_run);
        CHECK(result->verbose);
    }
}
