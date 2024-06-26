#include "gucc/file_utils.hpp"

#include <cerrno>   // for errno, strerror
#include <cstdio>   // for feof, fgets, pclose, perror, popen
#include <cstdlib>  // for exit, WIFEXITED, WIFSIGNALED

#include <fstream>  // for ofstream

#include <spdlog/spdlog.h>

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

}  // namespace gucc::file_utils
