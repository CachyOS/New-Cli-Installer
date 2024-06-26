#include "gucc/io_utils.hpp"

#include <sys/wait.h>  // for waitpid
#include <unistd.h>    // for execvp, fork

#include <cstdint>  // for int32_t
#include <cstdio>   // for feof, fgets, pclose, popen
#include <cstdlib>  // for WIFEXITED, WIFSIGNALED

#include <algorithm>  // for transform
#include <fstream>    // for ofstream

#include <fmt/compile.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::utils {

auto safe_getenv(const char* env_name) noexcept -> std::string_view {
    const char* const raw_val = getenv(env_name);
    return raw_val != nullptr ? std::string_view{raw_val} : std::string_view{};
}

void exec(const std::vector<std::string>& vec) noexcept {
    const bool log_exec_cmds = utils::safe_getenv("LOG_EXEC_CMDS") == "1"sv;
    const bool dirty_cmd_run = utils::safe_getenv("DIRTY_CMD_RUN") == "1"sv;

    if (log_exec_cmds && spdlog::default_logger_raw() != nullptr) {
        spdlog::debug("[exec] cmd := {}", vec);
    }
    if (dirty_cmd_run) {
        return;
    }

    std::int32_t status{};
    auto pid = fork();
    if (pid == 0) {
        std::vector<char*> args;
        std::transform(vec.cbegin(), vec.cend(), std::back_inserter(args),
            [=](const std::string& arg) -> char* { return const_cast<char*>(arg.data()); });
        args.push_back(nullptr);

        char** command = args.data();
        execvp(command[0], command);
    } else {
        do {
            waitpid(pid, &status, 0);
        } while ((!WIFEXITED(status)) && (!WIFSIGNALED(status)));
    }
}

// https://github.com/sheredom/subprocess.h
// https://gist.github.com/konstantint/d49ab683b978b3d74172
// https://github.com/arun11299/cpp-subprocess/blob/master/subprocess.hpp#L1218
// https://stackoverflow.com/questions/11342868/c-interface-for-interactive-bash
// https://github.com/hniksic/rust-subprocess
auto exec(std::string_view command, bool interactive) noexcept -> std::string {
    const bool log_exec_cmds = utils::safe_getenv("LOG_EXEC_CMDS") == "1"sv;

    if (log_exec_cmds && spdlog::default_logger_raw() != nullptr) {
        spdlog::debug("[exec] cmd := '{}'", command);
    }

    if (interactive) {
        const auto& ret_code = system(command.data());
        return std::to_string(ret_code);
    }

    const std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.data(), "r"), pclose);
    if (!pipe) {
        spdlog::error("popen failed! '{}'", command);
        return "-1";
    }

    std::string result{};
    std::array<char, 128> buffer{};
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
    }

    if (result.ends_with('\n')) {
        result.pop_back();
    }

    return result;
}

void arch_chroot(std::string_view command, std::string_view mountpoint, bool interactive) noexcept {
    // TODO(vnepogodin): refactor to move output into variable and print into log
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("arch-chroot {} {} 2>>/tmp/cachyos-install.log 2>&1"), mountpoint, command);

#ifdef NDEVENV
    gucc::utils::exec(cmd_formatted, interactive);
#else
    spdlog::info("Running with arch-chroot(interactive='{}'): '{}'", interactive, cmd_formatted);
#endif
}

}  // namespace gucc::utils
