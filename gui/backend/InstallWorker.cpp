#include "InstallWorker.hpp"

#include "cachyos/orchestrator.hpp"
#include "cachyos/partition_planner.hpp"

// import gucc
#include "gucc/process.hpp"

#include <ranges>
#include <utility>

#include <QStringList>

#include <spdlog/spdlog.h>

namespace {

// runs on the install thread; queued signal delivery hops to the GUI thread
class ProgressForwarder final : public cachyos::installer::InstallObserver {
 public:
    explicit ProgressForwarder(InstallWorker& worker) noexcept : m_worker(worker) { }

    void on_progress(const cachyos::installer::ProgressEvent& event) noexcept override {
        emit m_worker.progressChanged(event.fraction, QString::fromStdString(event.message));
    }

 private:
    InstallWorker& m_worker;
};

}  // namespace

InstallWorker::InstallWorker(QObject* parent)
  : QObject(parent), m_session(gucc::utils::default_runner()) {
}

void InstallWorker::setContext(cachyos::installer::InstallContext ctx,
    cachyos::installer::SystemSettings sys_settings,
    cachyos::installer::UserSettings user_settings,
    std::string root_password,
    std::string staged_device,
    std::vector<cachyos::installer::partition_planner::PartitionOp> staged_ops,
    std::vector<cachyos::installer::partition_planner::StagedMount> staged_mounts) {
    m_ctx          = std::move(ctx);
    m_sysSettings  = std::move(sys_settings);
    m_userSettings = std::move(user_settings);
    m_rootPassword = std::move(root_password);
    m_stagedDevice = std::move(staged_device);
    m_stagedOps    = std::move(staged_ops);
    m_stagedMounts = std::move(staged_mounts);
}

void InstallWorker::cancel() {
    m_session.cancel();
}

void InstallWorker::run() {
    spdlog::info("Install worker starting...");

    // destructive, so deferred from "Validate plan" to here
    if (!m_stagedOps.empty() || !m_stagedMounts.empty()) {
        emit progressChanged(0.0, QStringLiteral("Applying partition changes..."));
        auto selections = cachyos::installer::partition_planner::apply_partition_plan(
            m_stagedDevice, m_stagedOps, m_stagedMounts);
        if (!selections) {
            emit finished(false, QString::fromStdString(selections.error()));
            return;
        }
        if (m_session.cancelled()) {
            emit finished(false, QStringLiteral("Cancelled by user"));
            return;
        }
        m_ctx.strategy = cachyos::installer::partition_strategy::ApplyLayout{*std::move(selections)};
        // closed by the Cleanup step
        m_ctx.luks_mappers = m_stagedMounts
            | std::ranges::views::filter(&cachyos::installer::partition_planner::StagedMount::luks)
            | std::ranges::views::transform(&cachyos::installer::partition_planner::StagedMount::luks_mapper_name)
            | std::ranges::to<std::vector<std::string>>();
    }

    // log lines reach QML through the logger's callback sink set up in main()
    ProgressForwarder progress{*this};
    const auto progress_handle = m_session.add_observer(progress);

    const auto result = cachyos::installer::run(
        m_ctx, m_sysSettings, m_userSettings, m_rootPassword, m_session);

    if (result.success) {
        emit finished(true, {});
        return;
    }

    QStringList errors;
    errors.reserve(static_cast<qsizetype>(result.errors.size()));
    for (const auto& err : result.errors) {
        errors.append(QString::fromStdString(err));
    }
    emit finished(false, errors.join(QChar::fromLatin1('\n')));
}
