#ifndef CACHYOS_GUI_INSTALL_WORKER_HPP
#define CACHYOS_GUI_INSTALL_WORKER_HPP

#include "cachyos/types.hpp"

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

 signals:
    void progressChanged(double fraction, const QString& step);
    void finished(bool success, const QString& error);
    void logOutput(const QString& line);

 private:
    void emitProgress(int step, int total, const char* message);

    cachyos::installer::InstallContext m_ctx;
    cachyos::installer::SystemSettings m_sysSettings;
    cachyos::installer::UserSettings m_userSettings;
    std::string m_rootPassword;
};

#endif  // CACHYOS_GUI_INSTALL_WORKER_HPP
