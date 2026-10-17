#pragma once

#define TOML_EXCEPTIONS 0  // NOLINT(cppcoreguidelines-macro-usage)

// UB on malformed otherwise
#define TOML_ASSERT(expr) static_cast<void>(0)  // NOLINT(cppcoreguidelines-macro-usage)
#define TOML_ASSUME(expr) static_cast<void>(0)  // NOLINT(cppcoreguidelines-macro-usage)

#include <toml++/toml.h>
