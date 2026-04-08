#include "InstallerBackend.hpp"
#include "InstallWorker.hpp"

#include "cachyos/disk.hpp"
#include "cachyos/installer_data.hpp"
#include "cachyos/system.hpp"

#include "gucc/bootloader.hpp"
#include "gucc/locale.hpp"
#include "gucc/timezone.hpp"

#include <spdlog/spdlog.h>

namespace {

auto toQStringList(const std::vector<std::string>& vec) -> QStringList {
    QStringList result;
    result.reserve(static_cast<qsizetype>(vec.size()));
    for (const auto& s : vec) {
        result.append(QString::fromStdString(s));
    }
    return result;
}

template <std::size_t N>
auto toQStringList(const std::array<std::string_view, N>& arr) -> QStringList {
    QStringList result;
    result.reserve(static_cast<qsizetype>(N));
    for (const auto& sv : arr) {
        result.append(QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size())));
    }
    return result;
}

}  // namespace

InstallerBackend::InstallerBackend(QObject* parent)
  : QObject(parent) {
    m_isRoot = cachyos::installer::check_root();
}

InstallerBackend::~InstallerBackend() {
    if (m_workerThread.isRunning()) {
        m_workerThread.quit();
        m_workerThread.wait();
    }
}

void InstallerBackend::initialize() {
    spdlog::info("Initializing installer backend...");

    // Detect system mode
    const auto& system_result = cachyos::installer::detect_system();
    if (system_result) {
        m_isUefi = (system_result->system_mode == cachyos::installer::InstallContext::SystemMode::UEFI);
    }

    // Check connectivity
    m_isConnected = cachyos::installer::is_connected();

    emit systemInfoChanged();

    populateStaticLists();
    refreshDevices();

    spdlog::info("Backend initialized: UEFI={}, connected={}, root={}", m_isUefi, m_isConnected, m_isRoot);
}

void InstallerBackend::populateStaticLists() {
    using namespace cachyos::installer::data;

    m_filesystemList = toQStringList(available_filesystems);
    m_kernelList     = toQStringList(available_kernels);
    m_desktopList    = toQStringList(available_desktops);
    m_shellList      = toQStringList(available_shells);

    // Bootloaders depend on UEFI/BIOS
    const auto bios_mode    = m_isUefi ? "UEFI" : "BIOS";
    const auto& bootloaders = available_bootloaders(bios_mode);
    QStringList bl_list;
    bl_list.reserve(static_cast<qsizetype>(bootloaders.size()));
    for (const auto& bl : bootloaders) {
        bl_list.append(QString::fromUtf8(gucc::bootloader::bootloader_to_string(bl).data()));
    }
    m_bootloaderList = bl_list;

    // Locale/keymap/timezone from gucc
    m_localeList   = toQStringList(gucc::locale::get_possible_locales());
    m_keymapList   = toQStringList(gucc::locale::get_x11_keymap_layouts());
    m_timezoneList = toQStringList(gucc::timezone::get_available_timezones());

    emit listsPopulated();
}

void InstallerBackend::refreshDevices() {
    m_deviceList = toQStringList(cachyos::installer::data::get_device_list());
    emit deviceListChanged();
}

QStringList InstallerBackend::getAvailableMountOpts(const QString& fstype) {
    const auto& opts = cachyos::installer::get_available_mount_opts(fstype.toStdString());
    return toQStringList(opts);
}

void InstallerBackend::setCurrentPage(int page) {
    if (m_currentPage == page)
        return;
    m_currentPage = page;
    emit currentPageChanged();
    emit canGoNextChanged();
    emit canGoBackChanged();
}

bool InstallerBackend::canGoNext() const {
    return m_currentPage < pageCount() - 1 && !m_isInstalling;
}

bool InstallerBackend::canGoBack() const {
    return m_currentPage > 0 && !m_isInstalling;
}

void InstallerBackend::startInstallation(const QVariantMap& selections) {
    if (m_isInstalling)
        return;

    m_isInstalling    = true;
    m_installProgress = 0.0;
    m_installStatus   = QStringLiteral("Starting installation...");
    m_errorMessage.clear();

    emit isInstallingChanged();
    emit installProgressChanged();
    emit installStatusChanged();
    emit errorMessageChanged();

    // Build InstallContext from selections
    cachyos::installer::InstallContext ctx{};
    ctx.mountpoint  = "/mnt";
    ctx.system_mode = m_isUefi
        ? cachyos::installer::InstallContext::SystemMode::UEFI
        : cachyos::installer::InstallContext::SystemMode::BIOS;

    ctx.device          = selections.value("device").toString().toStdString();
    ctx.kernel          = selections.value("kernel").toString().toStdString();
    ctx.desktop         = selections.value("desktop").toString().toStdString();
    ctx.filesystem_name = selections.value("filesystem").toString().toStdString();
    ctx.keymap          = selections.value("keymap").toString().toStdString();

    const auto& bl_str = selections.value("bootloader").toString().toStdString();
    const auto& bl_opt = gucc::bootloader::bootloader_from_string(bl_str);
    if (bl_opt) {
        ctx.bootloader = *bl_opt;
    }

    ctx.server_mode = selections.value("serverMode").toBool();

    // System settings
    cachyos::installer::SystemSettings sys_settings{};
    sys_settings.hostname = selections.value("hostname").toString().toStdString();
    sys_settings.locale   = selections.value("locale").toString().toStdString();
    sys_settings.xkbmap   = selections.value("keymap").toString().toStdString();
    sys_settings.keymap   = sys_settings.xkbmap;
    sys_settings.timezone = selections.value("timezone").toString().toStdString();
    sys_settings.hw_clock = cachyos::installer::SystemSettings::HwClock::UTC;

    // User settings
    cachyos::installer::UserSettings user_settings{};
    user_settings.username = selections.value("username").toString().toStdString();
    user_settings.password = selections.value("userPassword").toString().toStdString();
    user_settings.shell    = selections.value("shell").toString().toStdString();

    const auto root_password = selections.value("rootPassword").toString().toStdString();

    // Create worker
    if (m_worker) {
        m_workerThread.quit();
        m_workerThread.wait();
        delete m_worker;
    }

    m_worker = new InstallWorker();
    m_worker->setContext(std::move(ctx), std::move(sys_settings),
        std::move(user_settings), root_password);
    m_worker->moveToThread(&m_workerThread);

    connect(&m_workerThread, &QThread::started, m_worker, &InstallWorker::run);
    connect(m_worker, &InstallWorker::progressChanged, this, &InstallerBackend::onWorkerProgress);
    connect(m_worker, &InstallWorker::finished, this, &InstallerBackend::onWorkerFinished);
    connect(m_worker, &InstallWorker::logOutput, this, &InstallerBackend::onWorkerLogOutput);

    m_workerThread.start();
}

void InstallerBackend::cancelInstallation() {
    // TODO(vnepogodin): implement cancellation signal to worker
    spdlog::warn("Installation cancellation requested (not yet implemented)");
}

void InstallerBackend::onWorkerProgress(double fraction, const QString& step) {
    m_installProgress = fraction;
    m_installStatus   = step;
    emit installProgressChanged();
    emit installStatusChanged();
}

void InstallerBackend::onWorkerFinished(bool success, const QString& error) {
    m_isInstalling = false;
    if (!success) {
        m_errorMessage = error;
    }
    emit isInstallingChanged();
    emit errorMessageChanged();
    emit installFinished(success, error);

    m_workerThread.quit();
    m_workerThread.wait();
}

void InstallerBackend::onWorkerLogOutput(const QString& line) {
    emit logLine(line);
}
