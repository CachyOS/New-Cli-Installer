#include "doctest_compatibility.h"

#include "cachyos/run_from_config.hpp"
#include "cachyos/types.hpp"

using cachyos::installer::headless_exit_code;
using cachyos::installer::ValidationResult;

TEST_CASE("headless exit code")
{
    SECTION("clean install exits 0")
    {
        REQUIRE_EQ(headless_exit_code(ValidationResult{.success = true, .errors = {}, .warnings = {}}), 0);
    }
    SECTION("install that finished with step warnings exits 2")
    {
        const ValidationResult result{.success = true, .errors = {}, .warnings = {"Failed to run chwd -a -f on /mnt"}};
        REQUIRE_EQ(headless_exit_code(result), 2);
    }
    SECTION("failed install exits 1, with or without warnings")
    {
        REQUIRE_EQ(headless_exit_code(ValidationResult{.success = false, .errors = {"config validation failed"}, .warnings = {}}), 1);
        const ValidationResult result{.success = false, .errors = {"Partitioning failed: exit 1"}, .warnings = {"earlier warning"}};
        REQUIRE_EQ(headless_exit_code(result), 1);
    }
}
