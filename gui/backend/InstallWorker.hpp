#ifndef CACHYOS_GUI_INSTALL_WORKER_HPP
#define CACHYOS_GUI_INSTALL_WORKER_HPP

#include "cachyos/partition_planner.hpp"
#include "cachyos/session.hpp"
#include "cachyos/types.hpp"

#include <string>
#include <vector>

#include <QObject>
#include <QString>

class InstallWorker : public QObject {
    Q_OBJECT

 public:
    explicit InstallWorker(QObject* parent = nullptr);

    void setContext(cachyos::installer::InstallContext ctx,
        cachyos::installer::SystemSettings sys_settings,
        cachyos::installer::UserSettings user_settings,
        std::string root_password,
        std::string staged_device,
        std::vector<cachyos::installer::partition_planner::PartitionOp> staged_ops,
        std::vector<cachyos::installer::partition_planner::StagedMount> staged_mounts);

 public slots:
    void run();
    /// Request cancellation of the in-flight install. Safe to call from the
    /// UI thread while `run()` is executing on the worker thread; the
    /// orchestrator stops between steps and SIGTERMs the running command.
    void cancel();

 signals:
    void progressChanged(double fraction, const QString& step);
    void finished(bool success, const QString& error);

 private:
    cachyos::installer::InstallContext m_ctx;
    cachyos::installer::SystemSettings m_sysSettings;
    cachyos::installer::UserSettings m_userSettings;
    std::string m_rootPassword;
    // Partition edits to apply (as root) before the orchestrator runs. Empty
    // unless a Manual/Alongside plan staged destructive ops at validate time.
    std::string m_stagedDevice;
    std::vector<cachyos::installer::partition_planner::PartitionOp> m_stagedOps;
    std::vector<cachyos::installer::partition_planner::StagedMount> m_stagedMounts;
    cachyos::installer::InstallSession m_session;
};

#endif  // CACHYOS_GUI_INSTALL_WORKER_HPP
