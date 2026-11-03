#pragma once

// import gucc
#include "gucc/error.hpp"

#include <expected>     // for expected
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move

#include <fmt/compile.h>
#include <fmt/format.h>

namespace cachyos::installer {

// NOTE: generally we can also use Result here
template <class T>
[[nodiscard]] auto lift(gucc::Result<T> result, std::string_view prefix = {}) -> std::expected<T, std::string> {
    return std::move(result).transform_error([prefix](gucc::Error err) {
        return prefix.empty() ? std::move(err.context) : fmt::format(FMT_COMPILE("{}: {}"), prefix, err.context);
    });
}

}  // namespace cachyos::installer
