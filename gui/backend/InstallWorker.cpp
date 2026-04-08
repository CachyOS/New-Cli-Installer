#include "InstallWorker.hpp"

#include "cachyos/disk.hpp"
#include "cachyos/orchestrator.hpp"
#include "cachyos/partition_planner.hpp"
#include "cachyos/session.hpp"

// import gucc
#include "gucc/process.hpp"

#include <spdlog/spdlog.h>

#include <utility>

InstallWorker::InstallWorker(QObject* parent)
  : QObject(parent) {
}

void InstallWorker::setContext(cachyos::installer::InstallContext ctx,
    cachyos::installer::SystemSettings sys_settings,
    cachyos::installer::UserSettings user_settings,
    std::string root_password,
    std::string staged_device,
    std::vector<cachyos::installer::partition_planner::PartitionOp> staged_ops,
    std::vector<cachyos::installer::partition_planner::StagedMount> staged_mounts,
    StagedZfs staged_zfs) {
    m_ctx          = std::move(ctx);
    m_sysSettings  = std::move(sys_settings);
    m_userSettings = std::move(user_settings);
    m_rootPassword = std::move(root_password);
    m_stagedDevice = std::move(staged_device);
    m_stagedOps    = std::move(staged_ops);
    m_stagedMounts = std::move(staged_mounts);
    m_stagedZfs    = std::move(staged_zfs);
}

void InstallWorker::cancel() {
    m_runner.cancel();
}

void InstallWorker::run() {
    spdlog::info("Install worker starting...");

    // A staged whole-disk ZFS root layout is destructive and needs root, so it
    // runs here on the worker thread. It erases the disk, creates the zpool +
    // datasets (optionally encrypted), mounts them (and the ESP) under the target,
    // and marks the install prepartitioned so the orchestrator skips its own
    // umount/partition/mount and lets fstab pick up the already-mounted datasets.
    if (m_stagedZfs.active) {
        emit progressChanged(0.0, QStringLiteral("Creating ZFS pool..."));
        const auto& bios_mode = m_ctx.system_mode == cachyos::installer::InstallContext::SystemMode::UEFI
            ? std::string_view{"UEFI"} : std::string_view{"BIOS"};
        auto zpool = cachyos::installer::apply_zfs_root_layout(
            m_stagedZfs.device, m_stagedZfs.zpool_name, m_stagedZfs.passphrase,
            bios_mode, m_ctx.bootloader, m_ctx.mountpoint);
        if (!zpool) {
            emit finished(false, QString::fromStdString(zpool.error()));
            return;
        }
        m_ctx.zfs_zpool_names = {*zpool};
        m_ctx.strategy        = cachyos::installer::partition_strategy::UseExisting{};
    }

    // Apply any staged partition edits first (destructive, needs root). This is
    // deferred from "Validate plan" so validation stays read-only; doing it here
    // keeps slow resizes off the UI thread and lets the result feed the
    // orchestrator's Partition step via the strategy.
    if (!m_stagedZfs.active && (!m_stagedOps.empty() || !m_stagedMounts.empty())) {
        emit progressChanged(0.0, QStringLiteral("Applying partition changes..."));
        auto selections = cachyos::installer::partition_planner::apply_partition_plan(
            m_stagedDevice, m_stagedOps, m_stagedMounts);
        if (!selections) {
            emit finished(false, QString::fromStdString(selections.error()));
            return;
        }
        m_ctx.strategy = cachyos::installer::partition_strategy::ApplyLayout{
            .selections = std::move(*selections),
        };
    }

    // Log lines reach the QML view through the logger's callback sink (set up in
    // main()), which carries every decorated record. Only progress needs a
    // dedicated callback here.
    m_runner.reset_cancel();
    const cachyos::installer::InstallSession session{
        .runner      = m_runner,
        .on_progress = [this](const cachyos::installer::ProgressEvent& ev) {
            emit progressChanged(ev.fraction, QString::fromStdString(ev.message));
        },
    };

    const auto result = cachyos::installer::run(
        m_ctx, m_sysSettings, m_userSettings, m_rootPassword, session);

    if (result.success) {
        emit finished(true, {});
        return;
    }

    QString joined;
    for (const auto& err : result.errors) {
        if (!joined.isEmpty()) {
            joined.append(QChar::fromLatin1('\n'));
        }
        joined.append(QString::fromStdString(err));
    }
    emit finished(false, joined);
}
