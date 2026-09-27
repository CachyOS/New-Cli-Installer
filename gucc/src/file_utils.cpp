#include "gucc/file_utils.hpp"

#include <cerrno>   // for errno
#include <cstdio>   // for feof, fgets, pclose, perror, popen
#include <cstdlib>  // for exit, WIFEXITED, WIFSIGNALED
#include <cstring>  // for strerror

#include <filesystem>    // for create_directories, copy_file
#include <fstream>       // for ifstream, ofstream
#include <string>        // for getline
#include <system_error>  // for error_code

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace gucc::file_utils {

auto read_whole_file(std::string_view filepath) noexcept -> std::string {
    // Use std::fopen because it's faster than std::ifstream
    auto* file = std::fopen(filepath.data(), "rb");
    if (file == nullptr) {
        spdlog::error("[READWHOLEFILE] '{}' read failed: {}", filepath, std::strerror(errno));
        return {};
    }

    std::fseek(file, 0u, SEEK_END);
    const auto size = static_cast<std::size_t>(std::ftell(file));
    std::fseek(file, 0u, SEEK_SET);

    std::string buf;
    buf.resize(size);

    const std::size_t read = std::fread(buf.data(), sizeof(char), size, file);
    if (read != size) {
        spdlog::error("[READWHOLEFILE] '{}' read failed: {}", filepath, std::strerror(errno));
        return {};
    }
    std::fclose(file);

    return buf;
}

auto read_first_line(std::string_view filepath) noexcept -> std::string {
    std::string line{};
    if (std::ifstream file{std::string{filepath}}; file) {
        std::getline(file, line);
    }
    return line;
}

auto write_to_file(std::string_view data, std::string_view filepath) noexcept -> bool {
    std::ofstream file{filepath.data()};
    if (!file.is_open()) {
        spdlog::error("[WRITE_TO_FILE] '{}' open failed: {}", filepath, std::strerror(errno));
        return false;
    }
    file << data;
    return true;
}

auto create_file_for_overwrite(std::string_view filepath, std::string_view data) noexcept -> bool {
    std::ofstream file{filepath.data(), std::ios::out | std::ios::trunc};
    if (!file.is_open()) {
        return false;
    }
    file << data;
    return true;
}

auto write_file(std::string_view filepath, std::string_view data) noexcept -> Result<void> {
    if (!create_file_for_overwrite(filepath, data)) {
        return make_error(ErrorCode::FileIo, fmt::format("failed to write {}: {}", filepath, std::strerror(errno)));
    }
    return {};
}

auto create_directories(std::string_view path) noexcept -> Result<void> {
    std::error_code err{};
    fs::create_directories(path, err);
    if (err) {
        return make_error(ErrorCode::FileIo, fmt::format("failed to create {}: {}", path, err.message()));
    }
    return {};
}

auto copy_file(std::string_view from, std::string_view to) noexcept -> Result<void> {
    std::error_code err{};
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, err);
    if (err) {
        return make_error(ErrorCode::FileIo, fmt::format("failed to copy {} to {}: {}", from, to, err.message()));
    }
    return {};
}

}  // namespace gucc::file_utils
