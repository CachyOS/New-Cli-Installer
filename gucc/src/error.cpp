#include "gucc/error.hpp"

#include <array>    // for array
#include <string_view>  // for string_view
#include <utility>      // for move

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace {

using gucc::ErrorCode;

constexpr std::array kCodeStrings{
    std::pair{ErrorCode::SubprocessFailed, "SubprocessFailed"sv},
    std::pair{ErrorCode::FileIo,           "FileIo"sv},
    std::pair{ErrorCode::ParseError,       "ParseError"sv},
    std::pair{ErrorCode::InvalidArgument,  "InvalidArgument"sv},
    std::pair{ErrorCode::NotFound,         "NotFound"sv},
    std::pair{ErrorCode::PermissionDenied, "PermissionDenied"sv},
    std::pair{ErrorCode::Unsupported,      "Unsupported"sv},
    std::pair{ErrorCode::Unknown,          "Unknown"sv},
};

[[nodiscard]] auto code_to_string(ErrorCode code) noexcept -> std::string_view {
    for (const auto& [c, s] : kCodeStrings) {
        if (c == code) return s;
    }
    return "Unknown"sv;
}

}  // namespace

namespace gucc {

auto make_error(ErrorCode code, std::string context) noexcept -> std::unexpected<Error> {
    return std::unexpected<Error>{Error{.code = code, .context = std::move(context)}};
}

auto to_string(const Error& error) noexcept -> std::string {
    return fmt::format(FMT_COMPILE("{}: {}"), code_to_string(error.code), error.context);
}

}  // namespace gucc
