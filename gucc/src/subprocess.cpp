#include "gucc/subprocess.hpp"
#include "gucc/io_utils.hpp"
#include "third_party/subprocess.h"

#include <cstdint>  // for int32_t

#include <algorithm>    // for transform
#include <array>        // for array
#include <bit>          // for bit_cast
#include <mutex>        // for scoped_lock, lock_guard
#include <string_view>  // for string_view_literals
#include <vector>       // for vector

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::utils {

struct SubProcess::Impl {
    subprocess_s proc{};
};

SubProcess::SubProcess() : m_impl(std::make_unique<Impl>()) { }
SubProcess::~SubProcess() = default;

SubProcess::SubProcess(SubProcess&& other) noexcept
  : m_impl(std::move(other.m_impl)),
    m_process_log([&] {
        const std::lock_guard<std::mutex> lock(other.m_log_mutex);
        return std::move(other.m_process_log);
    }()) {
    running.store(other.running.load());
}

auto SubProcess::operator=(SubProcess&& other) noexcept -> SubProcess& {
    if (this != &other) {
        m_impl = std::move(other.m_impl);
        running.store(other.running.load());
        const std::scoped_lock lock(m_log_mutex, other.m_log_mutex);
        m_process_log = std::move(other.m_process_log);
    }
    return *this;
}

auto SubProcess::has_child() const noexcept -> bool {
    return m_impl && m_impl->proc.child != 0;
}

auto SubProcess::terminate() noexcept -> bool {
    return m_impl && subprocess_terminate(&m_impl->proc) == 0;
}

auto SubProcess::destroy() noexcept -> bool {
    return m_impl && subprocess_destroy(&m_impl->proc) == 0;
}

auto SubProcess::get_log() const noexcept -> std::string {
    const std::lock_guard<std::mutex> lock(m_log_mutex);
    return m_process_log;
}

void SubProcess::append_log(std::string_view text) noexcept {
    const std::lock_guard<std::mutex> lock(m_log_mutex);
    m_process_log += text;
}

auto exec_follow(const std::vector<std::string>& vec, SubProcess& child, bool async) noexcept -> bool {
    if (!async) {
        spdlog::error("Implement me!");
        return false;
    }

    const bool log_exec_cmds = utils::safe_getenv("LOG_EXEC_CMDS") == "1"sv;
    const bool dirty_cmd_run = utils::safe_getenv("DIRTY_CMD_RUN") == "1"sv;

    if (log_exec_cmds && spdlog::default_logger_raw() != nullptr) {
        spdlog::debug("[exec_follow] cmd := {}", vec);
    }
    if (dirty_cmd_run) {
        return true;
    }

    std::vector<char*> args;
    std::transform(vec.cbegin(), vec.cend(), std::back_inserter(args),
        [=](const std::string& arg) -> char* { return std::bit_cast<char*>(arg.data()); });
    args.push_back(nullptr);

    int32_t ret{0};
    subprocess_s process{};
    char** command                             = args.data();
    static constexpr const char* environment[] = {"PATH=/sbin:/bin:/usr/local/sbin:/usr/local/bin:/usr/bin:/usr/sbin", nullptr};
    if ((ret = subprocess_create_ex(command, subprocess_option_enable_async | subprocess_option_combined_stdout_stderr, environment, &process)) != 0) {
        child.running = false;
        return false;
    }

    child.m_impl->proc = process;

    std::array<char, 8192> buf{};
    std::uint32_t bytes_read{};
    do {
        bytes_read = subprocess_read_stdout(&process, buf.data(), static_cast<std::uint32_t>(buf.size()));
        if (bytes_read > 0) {
            child.append_log(std::string_view{buf.data(), bytes_read});
        }
    } while (bytes_read != 0);

    if (subprocess_join(&process, &ret) != 0) {
        spdlog::error("[exec_follow] Failed to join process: return code {}", ret);
        return false;
    }
    if (subprocess_destroy(&process) != 0) {
        spdlog::error("[exec_follow] Failed to destroy process");
        return false;
    }
    child.m_impl->proc = process;
    child.running      = false;
    // insert status for interactive
    child.append_log("\n----------DONE----------\n");
    return true;
}

}  // namespace gucc::utils
