#ifndef CACHYOS_GUI_INSTALLER_BACKEND_HPP
#define CACHYOS_GUI_INSTALLER_BACKEND_HPP

// import instlib
#include "cachyos/requirements.hpp"
#include "cachyos/types.hpp"

#include <optional>

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration>

class InstallWorker;

class InstallerBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Navigation
    Q_PROPERTY(int32_t currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int32_t pageCount READ pageCount CONSTANT)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY canGoNextChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)

    // System state
    Q_PROPERTY(bool isUefi READ isUefi NOTIFY systemInfoChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY systemInfoChanged)
    Q_PROPERTY(bool isRoot READ isRoot CONSTANT)

    // Welcome-page requirements (registry-driven; see cachyos/requirements.hpp)
    Q_PROPERTY(QVariantList requirements READ requirements NOTIFY requirementsChanged)
    Q_PROPERTY(bool mandatoryRequirementsSatisfied READ mandatoryRequirementsSatisfied NOTIFY requirementsChanged)

    // Data lists
    Q_PROPERTY(QStringList deviceList READ deviceList NOTIFY deviceListChanged)
    Q_PROPERTY(QStringList filesystemList READ filesystemList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList kernelList READ kernelList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList desktopList READ desktopList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList bootloaderList READ bootloaderList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList shellList READ shellList NOTIFY listsPopulated)
    Q_PROPERTY(QVariantList bootloaderOptions READ bootloaderOptions NOTIFY listsPopulated)
    Q_PROPERTY(QStringList localeList READ localeList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList keymapList READ keymapList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList timezoneList READ timezoneList NOTIFY listsPopulated)
    Q_PROPERTY(QStringList forbiddenLogins READ forbiddenLogins CONSTANT)
    Q_PROPERTY(QStringList forbiddenHostnames READ forbiddenHostnames CONSTANT)

    // Install progress
    Q_PROPERTY(bool isInstalling READ isInstalling NOTIFY isInstallingChanged)
    Q_PROPERTY(double installProgress READ installProgress NOTIFY installProgressChanged)
    Q_PROPERTY(QString installStatus READ installStatus NOTIFY installStatusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

    // Partition planner
    Q_PROPERTY(QString plannerError READ plannerError NOTIFY plannerErrorChanged)
    Q_PROPERTY(bool hasMountSelections READ hasMountSelections NOTIFY mountSelectionsChanged)

 public:
    explicit InstallerBackend(QObject* parent = nullptr);
    ~InstallerBackend() override;

    // Navigation
    [[nodiscard]] int32_t currentPage() const { return m_currentPage; }
    void setCurrentPage(int32_t page);
    [[nodiscard]] int32_t pageCount() const { return 11; }
    [[nodiscard]] bool canGoNext() const;
    [[nodiscard]] bool canGoBack() const;

    // System state
    [[nodiscard]] bool isUefi() const { return m_isUefi; }
    [[nodiscard]] bool isConnected() const { return m_isConnected; }
    [[nodiscard]] bool isRoot() const { return m_isRoot; }

    [[nodiscard]] QVariantList requirements() const { return m_requirements; }
    [[nodiscard]] bool mandatoryRequirementsSatisfied() const { return m_mandatoryRequirementsSatisfied; }

    // Data lists
    [[nodiscard]] QStringList deviceList() const { return m_deviceList; }
    [[nodiscard]] QStringList filesystemList() const { return m_filesystemList; }
    [[nodiscard]] QStringList kernelList() const { return m_kernelList; }
    [[nodiscard]] QStringList desktopList() const { return m_desktopList; }
    [[nodiscard]] QStringList bootloaderList() const { return m_bootloaderList; }
    [[nodiscard]] QStringList shellList() const { return m_shellList; }
    [[nodiscard]] QVariantList bootloaderOptions() const { return m_bootloaderOptions; }
    [[nodiscard]] QStringList localeList() const { return m_localeList; }
    [[nodiscard]] QStringList keymapList() const { return m_keymapList; }
    [[nodiscard]] QStringList timezoneList() const { return m_timezoneList; }
    [[nodiscard]] static QStringList forbiddenLogins();
    [[nodiscard]] static QStringList forbiddenHostnames();

    // Install progress
    [[nodiscard]] bool isInstalling() const { return m_isInstalling; }
    [[nodiscard]] double installProgress() const { return m_installProgress; }
    [[nodiscard]] QString installStatus() const { return m_installStatus; }
    [[nodiscard]] QString errorMessage() const { return m_errorMessage; }

    // Partition planner
    [[nodiscard]] QString plannerError() const { return m_plannerError; }
    [[nodiscard]] bool hasMountSelections() const { return m_mountSelections.has_value(); }

    Q_INVOKABLE void initialize();
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void refreshRequirements();
    Q_INVOKABLE QStringList getAvailableMountOpts(const QString& fstype) const;
    Q_INVOKABLE void startInstallation(const QVariantMap& selections);
    Q_INVOKABLE void cancelInstallation();

    // Reboots the host into the freshly installed system. Returns false if the
    // reboot command could not be dispatched (the caller stays in the live env).
    Q_INVOKABLE bool reboot() const;

    // Partition planner bridges. All "QVariantMap" / "QVariantList" payloads
    // mirror the structs in cachyos::installer::partition_planner so QML
    // never has to import planner headers.
    Q_INVOKABLE QVariantMap discoverDisks();
    Q_INVOKABLE QStringList inspectExistingBtrfs(const QString& device);
    Q_INVOKABLE QVariantList defaultBtrfsLayout() const;
    Q_INVOKABLE QVariantList defaultZfsLayout(const QString& zpool) const;
    Q_INVOKABLE QString suggestMountpointForSubvolume(const QString& subvol) const;
    Q_INVOKABLE bool prepareZpool(const QString& partition, const QString& zpool, const QString& mountpoint);
    Q_INVOKABLE bool encryptPartition(const QString& device, const QString& mapperName,
        const QString& passphrase, const QString& extraFlags,
        const QString& luksVersion = QStringLiteral("Luks2"));
    Q_INVOKABLE bool openEncryptedPartition(const QString& device, const QString& mapperName,
        const QString& passphrase);
    Q_INVOKABLE bool finalizePlan(const QVariantMap& plan);
    Q_INVOKABLE void clearMountSelections();

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
    void plannerErrorChanged();
    void mountSelectionsChanged();
    void requirementsChanged();

 private slots:
    void onWorkerProgress(double fraction, const QString& step);
    void onWorkerFinished(bool success, const QString& error);
    void onWorkerLogOutput(const QString& line);

 private:
    void populateStaticLists();

    int32_t m_currentPage{0};
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
    QVariantList m_bootloaderOptions;
    QStringList m_localeList;
    QStringList m_keymapList;
    QStringList m_timezoneList;

    QString m_plannerError;
    std::optional<cachyos::installer::MountSelections> m_mountSelections;

    QVariantList m_requirements;
    bool m_mandatoryRequirementsSatisfied{false};

    QThread m_workerThread;
    InstallWorker* m_worker{nullptr};

    void setPlannerError(QString error);
};

#endif  // CACHYOS_GUI_INSTALLER_BACKEND_HPP
