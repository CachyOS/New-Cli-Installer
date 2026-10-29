#pragma once

#include "cachyos/session.hpp"

// import gucc
#include "gucc/process.hpp"

#include <functional>   // for function
#include <string_view>  // for string_view

namespace tui::detail {

class LineObserver final : public gucc::utils::ExecutionObserver {
 public:
    using Callback = std::function<void(std::string_view)>;

    explicit LineObserver(Callback on_line) noexcept : m_on_line(std::move(on_line)) { }

    void on_line(const gucc::utils::CommandInfo& cmd, std::string_view line) noexcept override;

 private:
    Callback m_on_line;
};

class ScriptStdoutObserver final : public cachyos::installer::InstallObserver {
 public:
    explicit ScriptStdoutObserver(bool verbose = false) noexcept : m_verbose(verbose) { }

    void on_start(const gucc::utils::CommandInfo& cmd) noexcept override;
    void on_line(const gucc::utils::CommandInfo& cmd, std::string_view line) noexcept override;
    void on_finish(const gucc::utils::CommandInfo& cmd, const gucc::utils::ProcessResult& result) noexcept override;

    void on_step_begin(cachyos::installer::StepId step, std::size_t index, std::size_t total) noexcept override;
    void on_step_end(cachyos::installer::StepId step, cachyos::installer::StepStatus status, std::string_view detail) noexcept override;

 private:
    [[nodiscard]] auto shown(const gucc::utils::CommandInfo& cmd) const noexcept -> bool;

    // NOTE: read-only queries are shown only when verbose
    bool m_verbose{};
};

}  // namespace tui::detail
