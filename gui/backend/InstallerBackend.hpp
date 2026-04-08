#ifndef CACHYOS_GUI_INSTALLER_BACKEND_HPP
#define CACHYOS_GUI_INSTALLER_BACKEND_HPP

#include "cachyos/types.hpp"

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVariantMap>
#include <QtQmlIntegration>

class InstallWorker;

class InstallerBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Navigation
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int pageCount READ pageCount CONSTANT)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY canGoNextChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)

    // System state
    Q_PROPERTY(bool isUefi READ isUefi NOTIFY systemInfoChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY systemInfoChanged)
    Q_PROPERTY(bool isRoot READ isRoot CONSTANT)

    // Data lists
    Q_PROPERTY(QStringList deviceList READ deviceList NOTIFY deviceListChanged)
    Q_PROPERTY(QStringList filesystemList READ filesystemList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList kernelList READ kernelList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList desktopList READ desktopList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList bootloaderList READ bootloaderList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList shellList READ shellList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList localeList READ localeList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList keymapList READ keymapList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList timezoneList READ timezoneList NOTIFY listsPopulated)

    // Install progress
    Q_PROPERTY(bool isInstalling READ isInstalling NOTIFY isInstallingChanged)
    Q_PROPERTY(double installProgress READ installProgress NOTIFY installProgressChanged)
    Q_PROPERTY(QString installStatus READ installStatus NOTIFY installStatusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

 public:
    explicit InstallerBackend(QObject* parent = nullptr);
    ~InstallerBackend() override;

    // Navigation
    [[nodiscard]] int currentPage() const { return m_currentPage; }
    void setCurrentPage(int page);
    [[nodiscard]] int pageCount() const { return 10; }
    [[nodiscard]] bool canGoNext() const;
    [[nodiscard]] bool canGoBack() const;

    // System state
    [[nodiscard]] bool isUefi() const { return m_isUefi; }
    [[nodiscard]] bool isConnected() const { return m_isConnected; }
    [[nodiscard]] bool isRoot() const { return m_isRoot; }

    // Data lists
    [[nodiscard]] QStringList deviceList() const { return m_deviceList; }
    [[nodiscard]] QStringList filesystemList() const { return m_filesystemList; }
    [[nodiscard]] QStringList kernelList() const { return m_kernelList; }
    [[nodiscard]] QStringList desktopList() const { return m_desktopList; }
    [[nodiscard]] QStringList bootloaderList() const { return m_bootloaderList; }
    [[nodiscard]] QStringList shellList() const { return m_shellList; }
    [[nodiscard]] QStringList localeList() const { return m_localeList; }
    [[nodiscard]] QStringList keymapList() const { return m_keymapList; }
    [[nodiscard]] QStringList timezoneList() const { return m_timezoneList; }

    // Install progress
    [[nodiscard]] bool isInstalling() const { return m_isInstalling; }
    [[nodiscard]] double installProgress() const { return m_installProgress; }
    [[nodiscard]] QString installStatus() const { return m_installStatus; }
    [[nodiscard]] QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void initialize();
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE QStringList getAvailableMountOpts(const QString& fstype);
    Q_INVOKABLE void startInstallation(const QVariantMap& selections);
    Q_INVOKABLE void cancelInstallation();

 signals:
    void currentPageChanged();
    void canGoNextChanged();
    void canGoBackChanged();
    void systemInfoChanged();
    void deviceListChanged();
    void listsPopulated();
    void isInstallingChanged();
    void installProgressChanged();
    void installStatusChanged();
    void errorMessageChanged();
    void installFinished(bool success, const QString& error);
    void logLine(const QString& line);

 private slots:
    void onWorkerProgress(double fraction, const QString& step);
    void onWorkerFinished(bool success, const QString& error);
    void onWorkerLogOutput(const QString& line);

 private:
    void populateStaticLists();

    int m_currentPage{0};
    bool m_isUefi{false};
    bool m_isConnected{false};
    bool m_isRoot{false};
    bool m_isInstalling{false};
    double m_installProgress{0.0};
    QString m_installStatus;
    QString m_errorMessage;

    QStringList m_deviceList;
    QStringList m_filesystemList;
    QStringList m_kernelList;
    QStringList m_desktopList;
    QStringList m_bootloaderList;
    QStringList m_shellList;
    QStringList m_localeList;
    QStringList m_keymapList;
    QStringList m_timezoneList;

    QThread m_workerThread;
    InstallWorker* m_worker{nullptr};
};

#endif  // CACHYOS_GUI_INSTALLER_BACKEND_HPP
