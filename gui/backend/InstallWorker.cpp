#include "InstallWorker.hpp"

#include "cachyos/orchestrator.hpp"

#include <spdlog/spdlog.h>

#include <utility>

InstallWorker::InstallWorker(QObject* parent)
  : QObject(parent) {
}

void InstallWorker::setContext(cachyos::installer::InstallContext ctx,
    cachyos::installer::SystemSettings sys_settings,
    cachyos::installer::UserSettings user_settings,
    std::string root_password) {
    m_ctx          = std::move(ctx);
    m_sysSettings  = std::move(sys_settings);
    m_userSettings = std::move(user_settings);
    m_rootPassword = std::move(root_password);
    // Refresh the stop source so a previous cancel() doesn't poison the next run.
    m_stopSource = std::stop_source{};
}

void InstallWorker::cancel() {
    m_stopSource.request_stop();
}

void InstallWorker::run() {
    spdlog::info("Install worker starting...");

    const cachyos::installer::ExecutionCallbacks callbacks{
        .on_progress = [this](const cachyos::installer::ProgressEvent& ev) {
            emit progressChanged(ev.fraction, QString::fromStdString(ev.message));
        },
        .on_log_line = [this](std::string_view line) {
            emit logOutput(QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size())));
        },
    };

    const auto result = cachyos::installer::run(
        m_ctx, m_sysSettings, m_userSettings, m_rootPassword,
        callbacks, m_stopSource.get_token());

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
