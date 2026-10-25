#include "gucc/io_utils.hpp"
#include "gucc/logger.hpp"
#include "gucc/process.hpp"
#include "gucc/string_utils.hpp"

#include <unistd.h>  // for sync

#include <cstdlib>  // for getenv

#include <string>   // for string
#include <utility>  // for move
#include <vector>   // for vector

#include <fmt/compile.h>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

using gucc::utils::ProcessKind;
using gucc::utils::ProcessLocation;
using gucc::utils::ProcessResult;
using gucc::utils::RunOptions;

using gucc::ErrorCode;
using gucc::Result;

constexpr RunOptions kQuery{.quiet = true, .kind = ProcessKind::Query};
constexpr RunOptions kMutate{.kind = ProcessKind::Mutate};

constexpr auto in_target(RunOptions opts, std::string_view mountpoint) noexcept -> RunOptions {
    opts.location   = ProcessLocation::Target;
    opts.mountpoint = mountpoint;
    return opts;
}

constexpr auto command_line(std::string_view command) noexcept -> std::string_view {
    return command;
}

constexpr auto command_line(const std::vector<std::string>& argv) noexcept -> std::string {
    return gucc::utils::join(argv, ' ');
}

constexpr auto failure_context(std::string_view what, std::string_view command, std::string_view mountpoint, const ProcessResult& res) noexcept -> std::string {
    const auto shown  = gucc::logger::redact(command);
    const auto where  = mountpoint.empty() ? std::string{} : fmt::format(FMT_COMPILE(" in {}"), mountpoint);
    const auto status = [&res] {
        if (res.timed_out) {
            return std::string{"timed out"};
        }
        if (res.cancelled) {
            return std::string{"was cancelled"};
        }
        return fmt::format(FMT_COMPILE("exited with {}"), res.exit_code);
    }();
    if (what.empty()) {
        return fmt::format(FMT_COMPILE("'{}'{} {}"), shown, where, status);
    }
    return fmt::format(FMT_COMPILE("{}: '{}'{} {}"), what, shown, where, status);
}

// NOTE(vnepogodin): yenuenly that should be in Result as monadic
template <class Command>
constexpr auto or_error(const ProcessResult& res, std::string_view what, const Command& command, std::string_view mountpoint = {}) noexcept -> Result<void> {
    if (res.ok()) {
        return {};
    }
    return make_error(ErrorCode::SubprocessFailed, failure_context(what, command_line(command), mountpoint, res));
}

}  // namespace

namespace gucc::utils {

auto safe_getenv(const char* env_name) noexcept -> std::string_view {
    const char* const raw_val = getenv(env_name);
    return raw_val != nullptr ? std::string_view{raw_val} : std::string_view{};
}

auto exec(std::string_view command) noexcept -> std::string {
    return std::move(default_runner().run_shell(command, kQuery).output);
}

auto query_checked(std::string_view command) noexcept -> bool {
    return default_runner().run_shell(command, kQuery).ok();
}

auto query_output(std::string_view command) noexcept -> std::optional<std::string> {
    auto res = default_runner().run_shell(command, kQuery);
    if (!res.ok()) {
        return std::nullopt;
    }
    return std::move(res.output);
}

auto exec_checked(std::string_view command) noexcept -> bool {
    return default_runner().run_shell(command, kMutate).ok();
}

auto exec_checked_with_input(std::string_view command, std::string_view input) noexcept -> bool {
    auto opts  = kMutate;
    opts.input = input;
    return default_runner().run_shell(command, opts).ok();
}

auto exec_with_input(std::string_view command, std::string_view input) noexcept -> std::optional<std::string> {
    auto opts  = kQuery;
    opts.input = input;
    auto res   = default_runner().run_shell(command, opts);
    if (!res.ok()) {
        return std::nullopt;
    }
    return std::move(res.output);
}

auto query(const std::vector<std::string>& argv) noexcept -> std::string {
    return std::move(default_runner().run(argv, kQuery).output);
}

auto query_checked(const std::vector<std::string>& argv) noexcept -> bool {
    return default_runner().run(argv, kQuery).ok();
}

auto query_output(const std::vector<std::string>& argv) noexcept -> std::optional<std::string> {
    auto res = default_runner().run(argv, kQuery);
    if (!res.ok()) {
        return std::nullopt;
    }
    return std::move(res.output);
}

void exec(const std::vector<std::string>& argv) noexcept {
    default_runner().run(argv, kMutate);
}

auto exec_checked(const std::vector<std::string>& argv) noexcept -> bool {
    return default_runner().run(argv, kMutate).ok();
}

auto exec_checked(const std::vector<std::string>& argv, std::span<const std::string> env) noexcept -> bool {
    auto opts = kMutate;
    opts.env  = env;
    return default_runner().run(argv, opts).ok();
}

void arch_chroot(std::string_view command, std::string_view mountpoint) noexcept {
    default_runner().run_shell(command, in_target(kMutate, mountpoint));
}

auto arch_chroot_checked(std::string_view command, std::string_view mountpoint) noexcept -> bool {
    return default_runner().run_shell(command, in_target(kMutate, mountpoint)).ok();
}

auto arch_chroot_checked(const std::vector<std::string>& argv, std::string_view mountpoint) noexcept -> bool {
    return default_runner().run(argv, in_target(kMutate, mountpoint)).ok();
}

auto arch_chroot_output(std::string_view command, std::string_view mountpoint) noexcept -> std::optional<std::string> {
    auto res = default_runner().run_shell(command, in_target(kMutate, mountpoint));
    if (!res.ok()) {
        return std::nullopt;
    }
    return std::move(res.output);
}

auto arch_chroot_query(std::string_view command, std::string_view mountpoint) noexcept -> std::string {
    return std::move(default_runner().run_shell(command, in_target(kQuery, mountpoint)).output);
}

auto arch_chroot_query_checked(std::string_view command, std::string_view mountpoint) noexcept -> bool {
    return default_runner().run_shell(command, in_target(kQuery, mountpoint)).ok();
}

auto arch_chroot_query_checked(const std::vector<std::string>& argv, std::string_view mountpoint) noexcept -> bool {
    return default_runner().run(argv, in_target(kQuery, mountpoint)).ok();
}

auto exec_or_error(std::string_view command, std::string_view what) noexcept -> Result<void> {
    return or_error(default_runner().run_shell(command, kMutate), what, command);
}

auto exec_or_error(const std::vector<std::string>& argv, std::string_view what) noexcept -> Result<void> {
    return or_error(default_runner().run(argv, kMutate), what, argv);
}

auto exec_or_error(const std::vector<std::string>& argv, std::span<const std::string> envs, std::string_view what) noexcept -> Result<void> {
    auto opts = kMutate;
    opts.env  = envs;
    return or_error(default_runner().run(argv, opts), what, argv);
}

auto exec_or_error_with_input(std::string_view command, std::string_view input, std::string_view what) noexcept -> Result<void> {
    auto opts  = kMutate;
    opts.input = input;
    return or_error(default_runner().run_shell(command, opts), what, command);
}

auto exec_or_error_with_input(const std::vector<std::string>& argv, std::string_view input, std::string_view what) noexcept -> Result<void> {
    auto opts  = kMutate;
    opts.input = input;
    return or_error(default_runner().run(argv, opts), what, argv);
}

auto arch_chroot_or_error(std::string_view command, std::string_view mountpoint, std::string_view what) noexcept -> Result<void> {
    return or_error(default_runner().run_shell(command, in_target(kMutate, mountpoint)), what, command, mountpoint);
}

auto arch_chroot_or_error(const std::vector<std::string>& argv, std::string_view mountpoint, std::string_view what) noexcept -> Result<void> {
    return or_error(default_runner().run(argv, in_target(kMutate, mountpoint)), what, argv, mountpoint);
}

auto run_pacstrap(std::string_view mountpoint, std::string_view packages, std::string_view pacman_config, bool hostcache) noexcept -> bool {
    const auto cache_flag  = hostcache ? "-c"sv : ""sv;
    const auto config_flag = pacman_config.empty()
        ? std::string{}
        : fmt::format(FMT_COMPILE("-C {}"), pacman_config);
    // TODO(vnepogodin): pacstrap should be more customizable and be in it's own "module"
    const auto& cmd_formatted = fmt::format(FMT_COMPILE("pacstrap {} {} {} {}"), cache_flag, config_flag, mountpoint, packages);

    spdlog::info("Running pacstrap with packages: '{}'", packages);
    return exec_checked(cmd_formatted);
}

void settle_devices() noexcept {
    // a failure here must never abort the install
    if (!exec_checked("udevadm settle"sv)) {
        spdlog::warn("Could not settle disks");
    }
    ::sync();
}

}  // namespace gucc::utils
