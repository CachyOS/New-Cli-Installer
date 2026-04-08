#include "gucc/logger.hpp"

#include <mutex>        // for mutex, lock_guard
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

// Secret scrubbing for the logger. Kept in its own always-compiled translation
// unit (unlike logger.cpp, which is dropped from COS_BUILD_STATIC builds)
// because gucc's own command-execution paths call redact() unconditionally.

namespace gucc::logger {

namespace {

constexpr std::string_view kRedactionMask = "<redacted>";

std::mutex g_secrets_mutex;
std::vector<std::string> g_secrets;

}  // namespace

void register_secret(std::string_view secret) noexcept {
    if (secret.empty()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_secrets_mutex);
    for (const auto& existing : g_secrets) {
        if (existing == secret) {
            return;
        }
    }
    g_secrets.emplace_back(secret);
}

void clear_secrets() noexcept {
    const std::lock_guard<std::mutex> lock(g_secrets_mutex);
    g_secrets.clear();
}

auto redact(std::string_view line) noexcept -> std::string {
    const std::lock_guard<std::mutex> lock(g_secrets_mutex);
    if (g_secrets.empty()) {
        return std::string{line};
    }
    std::string result{line};
    for (const auto& secret : g_secrets) {
        std::string::size_type pos{};
        while ((pos = result.find(secret, pos)) != std::string::npos) {
            result.replace(pos, secret.size(), kRedactionMask);
            pos += kRedactionMask.size();
        }
    }
    return result;
}

}  // namespace gucc::logger
