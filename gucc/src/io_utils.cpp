#include "gucc/io_utils.hpp"
#include "gucc/process.hpp"

#include <cstdlib>  // for getenv, system

#include <string>   // for string, to_string
#include <utility>  // for move
#include <vector>   // for vector

#include <fmt/compile.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace gucc::utils {

auto safe_getenv(const char* env_name) noexcept -> std::string_view {
    const char* const raw_val = getenv(env_name);
    return raw_val != nullptr ? std::string_view{raw_val} : std::string_view{};
}

void exec(const std::vector<std::string>& vec) noexcept {
    default_runner().run(vec, RunOptions{.kind = ProcessKind::Mutate});
}

// https://github.com/sheredom/subprocess.h
// https://gist.github.com/konstantint/d49ab683b978b3d74172
// https://github.com/arun11299/cpp-subprocess/blob/master/subprocess.hpp#L1218
// https://stackoverflow.com/questions/11342868/c-interface-for-interactive-bash
// https://github.com/hniksic/rust-subprocess
auto exec(std::string_view command, bool interactive) noexcept -> std::string {
    if (interactive) {
        const auto ret_code = std::system(command.data());
        return std::to_string(ret_code);
    }

    return std::move(default_runner().run_shell(command, RunOptions{.quiet = true, .kind = ProcessKind::Mutate}).output);
}

auto exec_checked(std::string_view command) noexcept -> bool {
    return default_runner().run_shell(command, RunOptions{.kind = ProcessKind::Mutate}).ok();
}

void arch_chroot(std::string_view command, std::string_view mountpoint, [[maybe_unused]] bool interactive) noexcept {
#ifdef NDEVENV
    default_runner().run_shell(command, RunOptions{.location = ProcessLocation::Target, .mountpoint = mountpoint});
#else
    spdlog::info("Running with arch-chroot(interactive='{}') on '{}': '{}'", interactive, mountpoint, command);
#endif
}

auto arch_chroot_checked(std::string_view command, std::string_view mountpoint) noexcept -> bool {
#ifdef NDEVENV
    return default_runner().run_shell(command, RunOptions{.location = ProcessLocation::Target, .mountpoint = mountpoint}).ok();
#else
    spdlog::info("Running with checked arch-chroot on '{}': '{}'", mountpoint, command);
    return true;
#endif
}

auto arch_chroot_follow(std::string_view command, std::string_view mountpoint) noexcept -> bool {
#ifdef NDEVENV
    return default_runner().run_shell(command, RunOptions{.location = ProcessLocation::Target, .mountpoint = mountpoint}).ok();
#else
    spdlog::info("Running with arch-chroot-follow on '{}': '{}'", mountpoint, command);
    return true;
#endif
}

auto run_pacstrap(std::string_view mountpoint, std::string_view packages, bool hostcache) noexcept -> bool {
    const auto& cmd = hostcache ? "pacstrap"sv : "pacstrap -c"sv;
    // TODO(vnepogodin): pacstrap should be more customizable and be in it's own "module"
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("{} {} {} 2>&1 | tee -a /tmp/pacstrap.log"), cmd, mountpoint, packages);

    spdlog::info("Running pacstrap with packages: '{}'", packages);
#ifdef NDEVENV
    return default_runner().run_shell(cmd_formatted, RunOptions{.kind = ProcessKind::Mutate}).ok();
#else
    spdlog::info("Running command: '{}'", cmd_formatted);
    return true;
#endif
}

}  // namespace gucc::utils
