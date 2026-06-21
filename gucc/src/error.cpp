#include "gucc/error.hpp"

#include <string_view>  // for string_view
#include <utility>      // for move

#include <fmt/compile.h>
#include <fmt/format.h>

using namespace std::string_view_literals;

namespace {

using gucc::ErrorCode;

// TODO(vnepogodin): refactor that using staticmap enum
[[nodiscard]] auto code_to_string(ErrorCode code) noexcept -> std::string_view {
    switch (code) {
    case ErrorCode::SubprocessFailed:
        return "SubprocessFailed"sv;
    case ErrorCode::FileIo:
        return "FileIo"sv;
    case ErrorCode::ParseError:
        return "ParseError"sv;
    case ErrorCode::InvalidArgument:
        return "InvalidArgument"sv;
    case ErrorCode::NotFound:
        return "NotFound"sv;
    case ErrorCode::PermissionDenied:
        return "PermissionDenied"sv;
    case ErrorCode::Unsupported:
        return "Unsupported"sv;
    case ErrorCode::Unknown:
        return "Unknown"sv;
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
