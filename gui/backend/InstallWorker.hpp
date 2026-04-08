#ifndef CACHYOS_GUI_INSTALL_WORKER_HPP
#define CACHYOS_GUI_INSTALL_WORKER_HPP

#include "cachyos/types.hpp"

#include <stop_token>

#include <QObject>
#include <QString>

class InstallWorker : public QObject {
    Q_OBJECT

 public:
    explicit InstallWorker(QObject* parent = nullptr);

    void setContext(cachyos::installer::InstallContext ctx,
        cachyos::installer::SystemSettings sys_settings,
        cachyos::installer::UserSettings user_settings,
        std::string root_password);

 public slots:
    void run();
    /// Request cancellation of the in-flight install. Safe to call from the
    /// UI thread while `run()` is executing on the worker thread; the
    /// orchestrator checks the stop token between every step and forwards
    /// it into any subprocess it has spawned (which receives SIGTERM).
    void cancel();

 signals:
    void progressChanged(double fraction, const QString& step);
    void finished(bool success, const QString& error);
    void logOutput(const QString& line);

 private:
    cachyos::installer::InstallContext m_ctx;
    cachyos::installer::SystemSettings m_sysSettings;
    cachyos::installer::UserSettings m_userSettings;
    std::string m_rootPassword;
    std::stop_source m_stopSource;
};

#endif  // CACHYOS_GUI_INSTALL_WORKER_HPP
