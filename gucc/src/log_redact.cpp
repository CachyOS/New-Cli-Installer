#include "gucc/logger.hpp"

#include <algorithm>    // for contains
#include <atomic>       // for atomic
#include <mutex>        // for mutex, lock_guard
#include <ranges>       // for ranges::*
#include <string>       // for string
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace {

constexpr std::string_view kRedactionMask = "<redacted>";

constinit std::mutex g_secrets_mutex;
constinit std::vector<std::string> g_secrets;
constinit std::atomic<bool> g_has_secrets{false};

}  // namespace

namespace gucc::logger {

// TODO(vnepogodin): refactor later
void register_secret(std::string_view secret) noexcept {
    if (secret.empty()) {
        return;
    }

    const std::lock_guard<std::mutex> lock(g_secrets_mutex);
    if (std::ranges::contains(g_secrets, secret)) {
        return;
    }
    g_secrets.emplace_back(secret);
    g_has_secrets.store(true, std::memory_order_relaxed);
}

void clear_secrets() noexcept {
    const std::lock_guard<std::mutex> lock(g_secrets_mutex);
    g_secrets.clear();
    g_has_secrets.store(false, std::memory_order_relaxed);
}

auto has_secrets() noexcept -> bool {
    return g_has_secrets.load(std::memory_order_relaxed);
}

auto redact(std::string_view line) noexcept -> std::string {
    if (!has_secrets()) {
        return std::string{line};
    }

    const std::lock_guard<std::mutex> lock(g_secrets_mutex);
    std::string result{line};
    for (const auto& secret : g_secrets) {
        std::size_t pos{};
        while ((pos = result.find(secret, pos)) != std::string::npos) {
            result.replace(pos, secret.size(), kRedactionMask);
            pos += kRedactionMask.size();
        }
    }
    return result;
}

}  // namespace gucc::logger
