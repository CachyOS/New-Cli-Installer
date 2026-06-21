#pragma once

#include <cstdint>  // for uint8_t

#include <expected>  // for expected, unexpected
#include <string>    // for string

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
///
/// @code
/// return gucc::make_error(ErrorCode::FileIo, fmt::format("open {}: {}", path, err));
/// @endcode
[[nodiscard]] auto make_error(ErrorCode code, std::string context) noexcept -> std::unexpected<Error>;

/// @brief Format an Error as "code: context", handy for logs.
[[nodiscard]] auto to_string(const Error& error) noexcept -> std::string;

}  // namespace gucc
