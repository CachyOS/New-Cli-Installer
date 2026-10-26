#pragma once

#include "cachyos/types.hpp"

// import gucc
#include "gucc/process.hpp"

#include <cstddef>  // for size_t
#include <cstdint>  // for uint8_t

#include <mutex>        // for mutex
#include <string_view>  // for string_view
#include <vector>       // for vector

namespace cachyos::installer {

/// In execution order.
enum class StepId : std::uint8_t {
    Umount,
    Partition,
    Base,
    Fstab,
    EncryptSwap,
    SystemSettings,
    Users,
    MachineId,
    Desktop,
    DesktopConfigure,
    ServerPackages,
    SshKeys,
    ServerFirewall,
    Autologin,
    Chwd,
    NetworkCarryover,
    Bootloader,
    DetectCrypto,
    EnableServices,
    FinalValidation,
    BtrfsSnapshot,
    Cleanup,
};

enum class StepStatus : std::uint8_t {
    Ok,
    Warning,
    Failed,
    Cancelled,
};

[[nodiscard]] auto step_label(StepId step) noexcept -> std::string_view;

/// @brief Callbacks run on the install thread.
class InstallObserver : public gucc::utils::ExecutionObserver {
 public:
    virtual void on_step_begin([[maybe_unused]] StepId step, [[maybe_unused]] std::size_t index, [[maybe_unused]] std::size_t total) noexcept { }
    virtual void on_step_end([[maybe_unused]] StepId step, [[maybe_unused]] StepStatus status, [[maybe_unused]] std::string_view detail) noexcept { }
    virtual void on_progress([[maybe_unused]] const ProgressEvent& event) noexcept { }
};

class InstallSession final {
 public:
    explicit InstallSession(gucc::utils::ProcessRunner& runner) noexcept;
    ~InstallSession() = default;

    InstallSession(const InstallSession&)                    = delete;
    InstallSession(InstallSession&&)                         = delete;
    auto operator=(const InstallSession&) -> InstallSession& = delete;
    auto operator=(InstallSession&&) -> InstallSession&      = delete;

    [[nodiscard]] auto runner() const noexcept -> gucc::utils::ProcessRunner& { return m_runner; }

    // NOTE: observer is not owned.
    [[nodiscard]] auto add_observer(InstallObserver& observer) noexcept -> gucc::utils::ObserverHandle;

    /// SIGTERMs the running command and stop before starting other steps.
    void cancel() const noexcept { m_runner.cancel(); }
    [[nodiscard]] auto cancelled() const noexcept -> bool { return m_runner.cancelled(); }

    // called by the orchestrator.
    void notify_step_begin(StepId step, std::size_t index, std::size_t total) const noexcept;
    void notify_step_end(StepId step, StepStatus status, std::string_view detail) const noexcept;
    void notify_progress(const ProgressEvent& event) const noexcept;

 private:
    [[nodiscard]] auto observers_snapshot() const -> std::vector<InstallObserver*>;

    gucc::utils::ProcessRunner& m_runner;
    mutable std::mutex m_mutex;
    std::vector<InstallObserver*> m_observers;
};

}  // namespace cachyos::installer
