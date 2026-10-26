#include "cachyos/session.hpp"

#include <array>    // for array
#include <mutex>    // for scoped_lock
#include <utility>  // for move, to_underlying

using namespace std::string_view_literals;

namespace {

// TODO(vnepogodin): this still needs to be gone. order of labels shouldn't matter
constexpr std::array kStepLabels{
    "Unmounting existing partitions..."sv,
    "Partitioning and mounting..."sv,
    "Installing base system (this may take a while)..."sv,
    "Generating fstab..."sv,
    "Configuring encrypted swap..."sv,
    "Configuring system settings..."sv,
    "Creating user accounts..."sv,
    "Generating machine ID..."sv,
    "Installing desktop environment..."sv,
    "Configuring desktop environment..."sv,
    "Installing server profile packages..."sv,
    "Installing SSH keys..."sv,
    "Configuring server firewall..."sv,
    "Configuring autologin..."sv,
    "Installing hardware-driver profiles..."sv,
    "Carrying network connections forward..."sv,
    "Installing bootloader..."sv,
    "Detecting encryption state..."sv,
    "Enabling system services..."sv,
    "Running final validation..."sv,
    "Creating installation snapshot..."sv,
    "Cleaning up..."sv,
};
static_assert(kStepLabels.size() == std::to_underlying(cachyos::installer::StepId::Cleanup) + 1);

}  // namespace

namespace cachyos::installer {

auto step_label(StepId step) noexcept -> std::string_view {
    return kStepLabels.at(std::to_underlying(step));
}

InstallSession::InstallSession(gucc::utils::ProcessRunner& runner) noexcept : m_runner(runner) { }

auto InstallSession::add_observer(InstallObserver& observer) noexcept -> gucc::utils::ObserverHandle {
    auto runner_handle = m_runner.add_observer(observer);
    {
        const std::scoped_lock lock(m_mutex);
        m_observers.push_back(&observer);
    }
    return gucc::utils::ObserverHandle{[this, target = &observer, runner_handle = std::move(runner_handle)]() mutable noexcept {
        runner_handle.reset();
        const std::scoped_lock lock(m_mutex);
        std::erase(m_observers, target);
    }};
}

auto InstallSession::observers_snapshot() const -> std::vector<InstallObserver*> {
    const std::scoped_lock lock(m_mutex);
    return m_observers;
}

void InstallSession::notify_step_begin(StepId step, std::size_t index, std::size_t total) const noexcept {
    for (auto* observer : observers_snapshot()) {
        observer->on_step_begin(step, index, total);
    }
}

void InstallSession::notify_step_end(StepId step, StepStatus status, std::string_view detail) const noexcept {
    for (auto* observer : observers_snapshot()) {
        observer->on_step_end(step, status, detail);
    }
}

void InstallSession::notify_progress(const ProgressEvent& event) const noexcept {
    for (auto* observer : observers_snapshot()) {
        observer->on_progress(event);
    }
}

}  // namespace cachyos::installer
