#include "install_observers.hpp"

#include <cstdio>  // for stdout, fflush, fputc

#include <utility>  // for move

#include <fmt/color.h>
#include <fmt/core.h>

namespace {

template <typename... Args>
void print_line(const fmt::text_style& style, fmt::format_string<Args...> format, Args&&... args) {
    fmt::print(style, format, std::forward<Args>(args)...);
    // should be also handled via fmtlib
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace

namespace tui::detail {

void LineObserver::on_line([[maybe_unused]] const gucc::utils::CommandInfo& cmd, std::string_view line) noexcept {
    m_on_line(line);
}

auto ScriptStdoutObserver::shown(const gucc::utils::CommandInfo& cmd) const noexcept -> bool {
    return m_verbose || cmd.options.kind == gucc::utils::ProcessKind::Mutate;
}

void ScriptStdoutObserver::on_start(const gucc::utils::CommandInfo& cmd) noexcept {
    if (!shown(cmd)) {
        return;
    }
    if (cmd.skipped) {
        print_line(fmt::fg(fmt::color::gray), "+ [dry-run] {}", cmd.display);
    } else {
        print_line(fmt::emphasis::bold, "+ {}", cmd.display);
    }
}

void ScriptStdoutObserver::on_line(const gucc::utils::CommandInfo& cmd, std::string_view line) noexcept {
    if (!shown(cmd)) {
        return;
    }
    fmt::print("{}\n", line);
    // should be also handled via fmtlib
    std::fflush(stdout);
}

void ScriptStdoutObserver::on_finish(const gucc::utils::CommandInfo& cmd, const gucc::utils::ProcessResult& result) noexcept {
    if (!shown(cmd) || cmd.skipped || result.ok()) {
        return;
    }
    if (result.timed_out) {
        print_line(fmt::fg(fmt::color::red), "! timed out: {}", cmd.display);
    } else if (result.cancelled) {
        print_line(fmt::fg(fmt::color::yellow), "! cancelled: {}", cmd.display);
    } else {
        print_line(fmt::fg(fmt::color::red), "! exit {}: {}", result.exit_code, cmd.display);
    }
}

void ScriptStdoutObserver::on_step_begin(cachyos::installer::StepId step, std::size_t index, std::size_t total) noexcept {
    print_line(fmt::fg(fmt::color::cyan) | fmt::emphasis::bold, "==> [{}/{}] {}", index + 1, total, cachyos::installer::step_label(step));
}

void ScriptStdoutObserver::on_step_end([[maybe_unused]] cachyos::installer::StepId step, cachyos::installer::StepStatus status, std::string_view detail) noexcept {
    using cachyos::installer::StepStatus;
    if (status == StepStatus::Failed || status == StepStatus::Cancelled) {
        print_line(fmt::fg(fmt::color::red) | fmt::emphasis::bold, "==> {}", detail);
    }
}

}  // namespace tui::detail
