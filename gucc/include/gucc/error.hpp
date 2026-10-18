#pragma once

#include <cstdint>  // for uint8_t

#include <expected>  // for expected, unexpected
#include <string>    // for string
#include <utility>   // for forward

#include <fmt/compile.h>
#include <fmt/format.h>

namespace gucc {

/// @brief Broad category of a gucc failure.
enum class ErrorCode : std::uint8_t {
    SubprocessFailed,  ///< spawned command didn't run, or exited non-zero
    FileIo,            ///< read/write/stat of a file or path failed
    ParseError,        ///< couldn't parse the input into what we expected
    InvalidArgument,   ///< caller passed something the function won't accept
    NotFound,          ///< file/device/entry isn't there
    PermissionDenied,  ///< not enough privileges
    Unsupported,       ///< not implemented, or not valid for this input
    Unknown,           ///< fallback when nothing else fits
};

/// @brief A gucc failure.
struct Error final {
    ErrorCode code{ErrorCode::Unknown};
    std::string context;  ///< what went wrong, in words
};

/// @brief Result type for gucc operations that can fail.
///
/// On success you get a T, on failure an Error.
template <class T>
using Result = std::expected<T, Error>;

/// @brief Make a failed result from a code and a message.
[[nodiscard]] auto make_error(ErrorCode code, std::string context) noexcept -> std::unexpected<Error>;

/// @brief Make a failed result, formatting its message.
///
/// @code
/// return gucc::make_error(ErrorCode::FileIo, FMT_COMPILE("open {}: {}"), path, err);
/// @endcode
template <class S, class... Args>
    requires fmt::is_compiled_string<S>::value
[[nodiscard]] auto make_error(ErrorCode code, const S& format, Args&&... args) noexcept -> std::unexpected<Error> {
    return make_error(code, fmt::format(format, std::forward<Args>(args)...));
}

/// @brief Format an Error as "code: context", handy for logs.
[[nodiscard]] auto to_string(const Error& error) noexcept -> std::string;

}  // namespace gucc
