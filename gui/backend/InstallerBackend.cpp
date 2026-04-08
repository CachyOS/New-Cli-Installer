#include "InstallerBackend.hpp"
#include "InstallWorker.hpp"

// import instlib
#include "cachyos/disk.hpp"
#include "cachyos/installer_data.hpp"
#include "cachyos/partition_planner.hpp"
#include "cachyos/requirements.hpp"
#include "cachyos/system.hpp"

// import gucc
#include "gucc/bootloader.hpp"
#include "gucc/io_utils.hpp"
#include "gucc/locale.hpp"
#include "gucc/package_list.hpp"
#include "gucc/timezone.hpp"

#include <algorithm>    // for ranges::all_of
#include <atomic>       // for atomic
#include <cstdint>      // for uint64_t
#include <string>       // for string
#include <string_view>  // for string_view
#include <utility>      // for move

#include <QFutureWatcher>
#include <QtConcurrentRun>

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

namespace {

[[nodiscard]] auto requirementsBypassed() noexcept -> bool {
    // Dev/test override: when set in the environment, the welcome page's
    // mandatory-requirement gate is disabled. Read once and cached.
    static const bool bypassed = qEnvironmentVariableIsSet("CACHYOS_INSTALLER_BYPASS_REQS");
    return bypassed;
}

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

// Built-in optional-software groups for the Packages page.
//
// TODO(vnepogodin): should be in net-profiles.toml instead
auto builtinNetinstallGroups() -> std::vector<gucc::profile::NetinstallGroup> {
    using gucc::profile::NetinstallGroup;
    return {
        {.name           = "Gaming Support",
            .description = "Steam, Lutris, Wine and the CachyOS gaming meta-packages.",
            .icon        = "sports_esports",
            .is_bundle   = true,
            .packages    = {"cachyos-gaming-meta", "cachyos-gaming-applications"}},
        {.name           = "Printing Support",
            .description = "CUPS printing stack and printer configuration tools.",
            .icon        = "print",
            .is_bundle   = true,
            .packages    = {"cups", "cups-filters", "cups-pdf", "foomatic-db",
                "foomatic-db-engine", "foomatic-db-gutenprint-ppds",
                "foomatic-db-nonfree", "foomatic-db-nonfree-ppds",
                "foomatic-db-ppds", "ghostscript", "gsfonts", "gutenprint",
                "system-config-printer"}},
        {.name           = "HP Printer / Scanner",
            .description = "Extra drivers for HP printers and scanners (HPLIP).",
            .icon        = "scanner",
            .is_bundle   = true,
            .packages    = {"hplip", "python-pyqt5", "python-reportlab"}},
        {.name           = "Multimedia & Codecs",
            .description = "Media players and audio/video codecs for everyday playback.",
            .icon        = "movie",
            .is_bundle   = true,
            .packages    = {"vlc", "mpv", "ffmpeg", "gst-plugins-good", "gst-plugins-bad",
                "gst-plugins-ugly", "gst-libav", "libdvdcss"}},
        {.name           = "Office Suite",
            .description = "LibreOffice productivity suite with spell-checking.",
            .icon        = "description",
            .is_bundle   = true,
            .packages    = {"libreoffice-fresh", "hunspell", "hunspell-en_us"}},
        {.name           = "Virtualization",
            .description = "Run virtual machines with QEMU/KVM and virt-manager.",
            .icon        = "computer",
            .is_bundle   = true,
            .packages    = {"qemu-full", "libvirt", "virt-manager", "edk2-ovmf", "dnsmasq",
                "dmidecode"}},
        {.name           = "Accessibility Tools",
            .description = "Screen reader and pointer aids for impaired vision.",
            .icon        = "accessibility",
            .is_bundle   = true,
            .packages    = {"espeak-ng", "orca", "mousetweaks"}},
        {.name           = "Development Tools",
            .description = "Compilers, version control and developer runtimes.",
            .icon        = "code",
            .is_bundle   = true,
            .packages    = {"base-devel", "git", "github-cli"},
            .subgroups   = {
                {.name           = "Containers",
                    .description = "Docker and Podman container tooling.",
                    .packages    = {"docker", "docker-compose", "docker-buildx", "podman"}},
                {.name           = "Language runtimes",
                    .description = "Popular programming-language toolchains.",
                    .packages    = {"python-pip", "nodejs", "npm", "jdk-openjdk", "rustup", "go"}},
                {.name           = "Editors & IDEs",
                    .description = "Text editors and IDEs for coding.",
                    .packages    = {"neovim", "code", "emacs"}},
            }},
    };
}

// Recursively map a gucc netinstall group tree to nested QVariantMaps so QML can
// consume it without importing gucc headers. Keys mirror NetinstallGroup fields.
auto netinstallGroupsToVariant(const std::vector<gucc::profile::NetinstallGroup>& groups) -> QVariantList {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(groups.size()));
    for (const auto& g : groups) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), QString::fromStdString(g.name));
        map.insert(QStringLiteral("description"), QString::fromStdString(g.description));
        map.insert(QStringLiteral("icon"), QString::fromStdString(g.icon));
        map.insert(QStringLiteral("selected"), g.selected);
        map.insert(QStringLiteral("hidden"), g.hidden);
        map.insert(QStringLiteral("critical"), g.critical);
        map.insert(QStringLiteral("bundle"), g.is_bundle);
        map.insert(QStringLiteral("packages"), toQStringList(g.packages));
        map.insert(QStringLiteral("subgroups"), netinstallGroupsToVariant(g.subgroups));
        result.append(map);
    }
    return result;
}

// Live backend instance used by forwardLogLine() to push spdlog records into the
// GUI log view. Set on construction, cleared on destruction. atomic so a logging
// thread can read it safely while the GUI thread (re)assigns it.
std::atomic<InstallerBackend*> g_logForwarder{nullptr};

}  // namespace

void InstallerBackend::forwardLogLine(const std::string& message) {
    auto* inst = g_logForwarder.load(std::memory_order_acquire);
    if (inst == nullptr) {
        return;
    }
    // Hop to the GUI thread; logging can happen on the install worker thread.
    const auto line = QString::fromStdString(message);
    QMetaObject::invokeMethod(
        inst, [inst, line]() { emit inst->logLine(line); }, Qt::QueuedConnection);
}

InstallerBackend::InstallerBackend(QObject* parent)
  : QObject(parent), m_isRoot(cachyos::installer::check_root()) {
    g_logForwarder.store(this, std::memory_order_release);
}

InstallerBackend::~InstallerBackend() {
    g_logForwarder.store(nullptr, std::memory_order_release);
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

    cachyos::installer::register_builtin_requirements();
    refreshRequirements();

    if (requirementsBypassed()) {
        spdlog::warn("CACHYOS_INSTALLER_BYPASS_REQS is set; welcome-page mandatory checks will not gate Next");
    }

    spdlog::info("Backend initialized: UEFI={}, connected={}, root={}", m_isUefi, m_isConnected, m_isRoot);
}

void InstallerBackend::refreshRequirements() {
    const auto results = cachyos::installer::check_requirements({});

    QVariantList out;
    out.reserve(static_cast<qsizetype>(results.size()));
    for (const auto& r : results) {
        QVariantMap row;
        row["id"]            = QString::fromStdString(r.id);
        row["messageOk"]     = QString::fromStdString(r.message_ok);
        row["messageFailed"] = QString::fromStdString(r.message_failed);
        row["satisfied"]     = r.satisfied;
        row["mandatory"]     = r.mandatory;
        out.append(row);
    }
    m_requirements = std::move(out);

    m_mandatoryRequirementsSatisfied = std::ranges::all_of(results,
        [](const auto& r) { return !r.mandatory || r.satisfied; });

    emit requirementsChanged();
    emit canGoNextChanged();
}

void InstallerBackend::populateStaticLists() {
    using namespace cachyos::installer::data;

    m_filesystemList = toQStringList(available_filesystems);
    m_kernelList     = toQStringList(available_kernels);
    m_desktopList    = toQStringList(available_desktops);
    m_shellList      = toQStringList(available_shells);

    // Bootloaders depend on UEFI/BIOS
    const auto& bios_mode   = m_isUefi ? "UEFI"sv : "BIOS"sv;
    const auto& bootloaders = available_bootloaders(bios_mode);
    QStringList bl_list;
    bl_list.reserve(static_cast<qsizetype>(bootloaders.size()));
    for (const auto& bl : bootloaders) {
        bl_list.append(QString::fromUtf8(gucc::bootloader::bootloader_to_string(bl).data()));
    }
    m_bootloaderList = bl_list;

    // Card metadata for the new bootloader chooser page.
    const auto& bl_options = cachyos::installer::data::bootloader_options(bios_mode);
    QVariantList bl_opts;
    bl_opts.reserve(static_cast<qsizetype>(bl_options.size()));
    for (const auto& opt : bl_options) {
        QVariantMap row;
        row["key"]         = QString::fromUtf8(opt.key.data(), static_cast<qsizetype>(opt.key.size()));
        row["label"]       = QString::fromUtf8(opt.label.data(), static_cast<qsizetype>(opt.label.size()));
        row["description"] = QString::fromUtf8(opt.description.data(), static_cast<qsizetype>(opt.description.size()));
        bl_opts.append(row);
    }
    m_bootloaderOptions = std::move(bl_opts);

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

QStringList InstallerBackend::forbiddenLogins() {
    return toQStringList(cachyos::installer::data::forbidden_logins);
}

QStringList InstallerBackend::forbiddenHostnames() {
    return toQStringList(cachyos::installer::data::forbidden_hostnames);
}

QStringList InstallerBackend::getAvailableMountOpts(const QString& fstype) const {
    const auto& opts = cachyos::installer::get_available_mount_opts(fstype.toStdString());
    return toQStringList(opts);
}

QStringList InstallerBackend::computeDesktopPackages(const QString& desktop) {
    // Resolve net-profile URLs from the InstallContext defaults so this matches
    // the list the installer actually pacstraps (see make_net_profs_info in
    // installer-lib/src/packages.cpp).
    const cachyos::installer::InstallContext defaults{};
    const gucc::package::NetProfileInfo info{
        .net_profs_url          = defaults.net_profiles_url,
        .net_profs_fallback_url = defaults.net_profiles_fallback_url,
    };
    const auto pkgs = gucc::package::get_pkglist_desktop(desktop.toStdString(), info);
    if (!pkgs.has_value()) {
        return {};
    }
    return toQStringList(*pkgs);
}

void InstallerBackend::fetchDesktopPackages(const QString& desktop) {
    // Record the latest request; coalesce so only one fetch is ever in flight
    // (the net-profiles cache has no internal locking).
    m_pkgFetchPending    = desktop;
    m_pkgFetchHasPending = true;
    if (!m_pkgFetchBusy) {
        startNextDesktopFetch();
    }
}

void InstallerBackend::startNextDesktopFetch() {
    if (!m_pkgFetchHasPending) {
        return;
    }
    m_pkgFetchHasPending  = false;
    m_pkgFetchBusy        = true;
    const QString desktop = m_pkgFetchPending;

    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, desktop]() {
        emit desktopPackagesReady(desktop, watcher->result());
        watcher->deleteLater();
        m_pkgFetchBusy = false;
        // Honour the most recent request that arrived while we were busy.
        startNextDesktopFetch();
    });
    watcher->setFuture(QtConcurrent::run(&InstallerBackend::computeDesktopPackages, desktop));
}

QVariantList InstallerBackend::computeNetinstallGroups() {
    // TODO(vnepogodin): should be in the net-profiles.toml
    const cachyos::installer::InstallContext defaults{};
    const gucc::package::NetProfileInfo info{
        .net_profs_url          = defaults.net_profiles_url,
        .net_profs_fallback_url = defaults.net_profiles_fallback_url,
    };
    const auto groups = gucc::package::get_netinstall_groups(info);
    if (groups.has_value() && !groups->empty()) {
        return netinstallGroupsToVariant(*groups);
    }
    return netinstallGroupsToVariant(builtinNetinstallGroups());
}

void InstallerBackend::fetchNetinstallGroups() {
    m_netFetchHasPending = true;
    if (!m_netFetchBusy) {
        startNetinstallFetch();
    }
}

void InstallerBackend::startNetinstallFetch() {
    if (!m_netFetchHasPending) {
        return;
    }
    m_netFetchHasPending = false;
    m_netFetchBusy       = true;

    auto* watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher]() {
        emit netinstallGroupsReady(watcher->result());
        watcher->deleteLater();
        m_netFetchBusy = false;
        startNetinstallFetch();
    });
    watcher->setFuture(QtConcurrent::run(&InstallerBackend::computeNetinstallGroups));
}

void InstallerBackend::setCurrentPage(int page) {
    if (m_currentPage == page) {
        return;
    }
    m_currentPage = page;
    emit currentPageChanged();
    emit canGoNextChanged();
    emit canGoBackChanged();
}

bool InstallerBackend::canGoNext() const {
    if (m_isInstalling || m_currentPage >= pageCount() - 1) {
        return false;
    }
    // Welcome page (index 0) blocks Next until all mandatory requirements pass.
    // The CACHYOS_INSTALLER_BYPASS_REQS env var disables this gate for UI testing.
    if (m_currentPage == 0 && !m_mandatoryRequirementsSatisfied && !requirementsBypassed()) {
        return false;
    }
    return true;
}

bool InstallerBackend::canGoBack() const {
    return m_currentPage > 0 && !m_isInstalling;
}

QString InstallerBackend::validateSelections(const QVariantMap& selections) const {
    const auto is_blank = [&selections](const QString& key) {
        return selections.value(key).toString().trimmed().isEmpty();
    };

    QStringList missing;
    if (is_blank(QStringLiteral("device"))) {
        missing << QStringLiteral("target device");
    }
    if (is_blank(QStringLiteral("kernel"))) {
        missing << QStringLiteral("kernel");
    }
    if (is_blank(QStringLiteral("locale"))) {
        missing << QStringLiteral("locale");
    }
    if (is_blank(QStringLiteral("timezone"))) {
        missing << QStringLiteral("timezone");
    }
    if (is_blank(QStringLiteral("keymap"))) {
        missing << QStringLiteral("keyboard layout");
    }
    if (is_blank(QStringLiteral("hostname"))) {
        missing << QStringLiteral("hostname");
    }
    if (is_blank(QStringLiteral("username"))) {
        missing << QStringLiteral("username");
    }
    if (!selections.value(QStringLiteral("serverMode")).toBool() && is_blank(QStringLiteral("desktop"))) {
        missing << QStringLiteral("desktop environment");
    }
    // The planner path (manual/replace) carries its own filesystem in the mount
    // selections, so only require an explicit filesystem for the auto layout.
    // A staged partition plan finalizes into mount selections at install time, so
    // it counts as having its own filesystem too.
    if (!m_mountSelections.has_value() && !hasStagedPartitioning() && !m_stagedZfsActive
        && is_blank(QStringLiteral("filesystem"))) {
        missing << QStringLiteral("filesystem");
    }

    if (!missing.isEmpty()) {
        return QStringLiteral("Missing required selection(s): %1.").arg(missing.join(QStringLiteral(", ")));
    }

    // The bootloader must resolve to a type the backend knows how to install.
    const auto bl_str = selections.value(QStringLiteral("bootloader")).toString().toStdString();
    const auto bl_opt = gucc::bootloader::bootloader_from_string(bl_str);
    if (!bl_opt) {
        return QStringLiteral("Unknown or unset bootloader selection.");
    }
    // grub cannot read a natively-encrypted ZFS pool.
    if (m_stagedZfsActive && m_stagedZfsEncrypt && *bl_opt == gucc::bootloader::BootloaderType::Grub) {
        return QStringLiteral("ZFS native encryption is not supported with the GRUB bootloader. "
                              "Choose another bootloader or disable ZFS encryption.");
    }
    return {};
}

bool InstallerBackend::startInstallation(const QVariantMap& selections) {
    if (m_isInstalling) {
        return false;
    }

    // Pre-flight: reject early so we never spawn a doomed install worker.
    if (const auto error = validateSelections(selections); !error.isEmpty()) {
        spdlog::error("Install pre-flight validation failed: {}", error.toStdString());
        m_errorMessage = error;
        emit errorMessageChanged();
        return false;
    }

    m_isInstalling    = true;
    m_isCancelling    = false;
    m_installProgress = 0.0;
    m_installStatus   = QStringLiteral("Starting installation...");
    m_errorMessage.clear();

    emit isInstallingChanged();
    emit isCancellingChanged();
    emit installProgressChanged();
    emit installStatusChanged();
    emit errorMessageChanged();

    // Build InstallContext from selections
    cachyos::installer::InstallContext ctx{};
    ctx.mountpoint  = "/mnt"sv;
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

    {
        const auto excluded = selections.value("excludedPackages").toStringList();
        ctx.excluded_packages.reserve(static_cast<std::size_t>(excluded.size()));
        for (const auto& pkg : excluded) {
            ctx.excluded_packages.push_back(pkg.toStdString());
        }
    }

    {
        // Optional packages picked on the Packages page (netinstall groups/bundles).
        const auto additional = selections.value("additionalPackages").toStringList();
        ctx.additional_packages.reserve(static_cast<std::size_t>(additional.size()));
        for (const auto& pkg : additional) {
            ctx.additional_packages.push_back(pkg.toStdString());
        }
    }

    if (m_mountSelections.has_value()) {
        ctx.mount_selections = m_mountSelections;
    }

    if (selections.contains(QStringLiteral("carryLiveNetwork"))) {
        ctx.carry_live_network = selections.value(QStringLiteral("carryLiveNetwork")).toBool();
    }
    // chwd hardware-profile installation is mandatory on CachyOS — always on.
    ctx.install_chwd_profiles = true;

    // Full-disk LUKS encryption for the default auto layout: stash the passphrase
    // so the orchestrator encrypts the auto-created root before mounting. Only the
    // pure auto path applies it — explicit/staged plans (mount_selections / staged
    // partitioning / staged zfs) carry their own root device.
    if (selections.value(QStringLiteral("encryptRoot")).toBool()
        && !m_mountSelections.has_value() && !hasStagedPartitioning() && !m_stagedZfsActive) {
        ctx.root_luks_passphrase = selections.value(QStringLiteral("luksPassphrase")).toString().toStdString();
    }

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
    user_settings.username  = selections.value("username").toString().toStdString();
    user_settings.password  = selections.value("userPassword").toString().toStdString();
    user_settings.shell     = selections.value("shell").toString().toStdString();
    user_settings.autologin = selections.value("autologin").toBool();

    const auto root_password = selections.value("rootPassword").toString().toStdString();

    // Create worker
    if (m_worker) {
        m_workerThread.quit();
        m_workerThread.wait();
        delete m_worker;
    }

    InstallWorker::StagedZfs staged_zfs{};
    if (m_stagedZfsActive) {
        staged_zfs.active     = true;
        staged_zfs.device     = m_stagedZfsDevice.toStdString();
        staged_zfs.zpool_name = m_stagedZfsPool.toStdString();
        if (m_stagedZfsEncrypt) {
            staged_zfs.passphrase = m_stagedZfsPassphrase.toStdString();
        }
    }

    m_worker = new InstallWorker();
    m_worker->setContext(std::move(ctx), std::move(sys_settings),
        std::move(user_settings), root_password,
        m_stagedPartDevice.toStdString(), m_stagedPartOps, m_stagedPartMounts,
        std::move(staged_zfs));
    m_worker->moveToThread(&m_workerThread);

    connect(&m_workerThread, &QThread::started, m_worker, &InstallWorker::run);
    connect(m_worker, &InstallWorker::progressChanged, this, &InstallerBackend::onWorkerProgress);
    connect(m_worker, &InstallWorker::finished, this, &InstallerBackend::onWorkerFinished);

    m_workerThread.start();
    return true;
}

void InstallerBackend::cancelInstallation() {
    if (m_worker == nullptr || !m_isInstalling || m_isCancelling) {
        return;
    }
    spdlog::info("Installation cancellation requested");
    m_isCancelling  = true;
    m_installStatus = QStringLiteral("Cancelling… waiting for the current step to stop.");
    emit isCancellingChanged();
    emit installStatusChanged();
    m_worker->cancel();
}

bool InstallerBackend::reboot() const {
    spdlog::info("Reboot into the installed system requested");
    if (!gucc::utils::exec_checked("systemctl reboot")) {
        spdlog::error("Failed to dispatch 'systemctl reboot'");
        return false;
    }
    return true;
}

void InstallerBackend::onWorkerProgress(double fraction, const QString& step) {
    m_installProgress = fraction;
    m_installStatus   = step;
    emit installProgressChanged();
    emit installStatusChanged();
}

void InstallerBackend::onWorkerFinished(bool success, const QString& error) {
    m_isInstalling = false;
    if (m_isCancelling) {
        m_isCancelling = false;
        emit isCancellingChanged();
    }
    if (!success) {
        m_errorMessage = error;
    }
    emit isInstallingChanged();
    emit errorMessageChanged();
    emit installFinished(success, error);

    m_workerThread.quit();
    m_workerThread.wait();
}

namespace {

namespace planner = cachyos::installer::partition_planner;

auto deviceEntryToVariant(const planner::DeviceEntry& d) -> QVariantMap {
    QVariantMap m;
    m.insert(QStringLiteral("name"), QString::fromStdString(d.name));
    m.insert(QStringLiteral("type"), QString::fromStdString(d.type));
    m.insert(QStringLiteral("fstype"), QString::fromStdString(d.fstype));
    m.insert(QStringLiteral("label"), QString::fromStdString(d.label));
    m.insert(QStringLiteral("model"), QString::fromStdString(d.model));
    m.insert(QStringLiteral("sizeBytes"), static_cast<qulonglong>(d.size_bytes));
    m.insert(QStringLiteral("parent"), QString::fromStdString(d.parent));
    m.insert(QStringLiteral("uuid"), QString::fromStdString(d.uuid));
    m.insert(QStringLiteral("partuuid"), QString::fromStdString(d.partuuid));
    QStringList mps;
    mps.reserve(static_cast<qsizetype>(d.mountpoints.size()));
    for (const auto& mp : d.mountpoints) {
        mps.append(QString::fromStdString(mp));
    }
    m.insert(QStringLiteral("mountpoints"), mps);
    return m;
}

auto btrfsSubvolumeToVariant(const planner::ExistingBtrfsSubvolume& s) -> QVariantMap {
    QVariantMap m;
    m.insert(QStringLiteral("device"), QString::fromStdString(s.device));
    m.insert(QStringLiteral("subvolume"), QString::fromStdString(s.subvolume));
    return m;
}

auto zfsPoolToVariant(const planner::ExistingZfsPool& p) -> QVariantMap {
    QVariantMap m;
    m.insert(QStringLiteral("name"), QString::fromStdString(p.name));
    return m;
}

auto lvmGroupToVariant(const planner::ExistingLvmGroup& g) -> QVariantMap {
    QVariantMap m;
    m.insert(QStringLiteral("name"), QString::fromStdString(g.name));
    m.insert(QStringLiteral("size"), QString::fromStdString(g.size));
    return m;
}

auto btrfsChoiceToVariant(const planner::BtrfsSubvolumeChoice& c) -> QVariantMap {
    QVariantMap m;
    m.insert(QStringLiteral("subvolume"), QString::fromStdString(c.subvolume));
    m.insert(QStringLiteral("mountpoint"), QString::fromStdString(c.mountpoint));
    return m;
}

auto zfsChoiceToVariant(const planner::ZfsDatasetChoice& c) -> QVariantMap {
    QVariantMap m;
    m.insert(QStringLiteral("dataset"), QString::fromStdString(c.dataset));
    m.insert(QStringLiteral("mountpoint"), QString::fromStdString(c.mountpoint));
    return m;
}

auto variantToRoot(const QVariantMap& m) -> cachyos::installer::RootPartitionSelection {
    return {
        .device           = m.value(QStringLiteral("device")).toString().toStdString(),
        .fstype           = m.value(QStringLiteral("fstype")).toString().toStdString(),
        .mkfs_command     = m.value(QStringLiteral("mkfsCommand")).toString().toStdString(),
        .mount_opts       = m.value(QStringLiteral("mountOpts")).toString().toStdString(),
        .format_requested = m.value(QStringLiteral("formatRequested"), true).toBool(),
    };
}

auto variantToSwap(const QVariantMap& m) -> cachyos::installer::SwapSelection {
    using Type           = cachyos::installer::SwapSelection::Type;
    const auto& type_str = m.value(QStringLiteral("type")).toString();
    Type type{Type::None};
    if (type_str == QStringLiteral("Swapfile")) {
        type = Type::Swapfile;
    } else if (type_str == QStringLiteral("Partition")) {
        type = Type::Partition;
    }
    return {
        .type          = type,
        .device        = m.value(QStringLiteral("device")).toString().toStdString(),
        .swapfile_size = m.value(QStringLiteral("swapfileSize")).toString().toStdString(),
        .needs_mkswap  = m.value(QStringLiteral("needsMkswap"), false).toBool(),
    };
}

auto variantToEsp(const QVariantMap& m) -> cachyos::installer::EspSelection {
    return {
        .device           = m.value(QStringLiteral("device")).toString().toStdString(),
        .mountpoint       = m.value(QStringLiteral("mountpoint")).toString().toStdString(),
        .format_requested = m.value(QStringLiteral("formatRequested"), true).toBool(),
    };
}

auto variantToAdditional(const QVariantMap& m) -> cachyos::installer::AdditionalPartSelection {
    return {
        .device           = m.value(QStringLiteral("device")).toString().toStdString(),
        .mountpoint       = m.value(QStringLiteral("mountpoint")).toString().toStdString(),
        .fstype           = m.value(QStringLiteral("fstype")).toString().toStdString(),
        .mkfs_command     = m.value(QStringLiteral("mkfsCommand")).toString().toStdString(),
        .mount_opts       = m.value(QStringLiteral("mountOpts")).toString().toStdString(),
        .format_requested = m.value(QStringLiteral("formatRequested"), true).toBool(),
    };
}

auto variantToBtrfsChoice(const QVariantMap& m) -> planner::BtrfsSubvolumeChoice {
    return {
        .subvolume  = m.value(QStringLiteral("subvolume")).toString().toStdString(),
        .mountpoint = m.value(QStringLiteral("mountpoint")).toString().toStdString(),
    };
}

// Parse the QML op list ([{ kind, … }]) into planner ops. On an unknown kind it
// returns an empty list and fills @p error.
auto variantToPartitionOps(const QVariantList& ops, QString& error) -> std::vector<planner::PartitionOp> {
    using Kind = planner::PartitionOp::Kind;
    std::vector<planner::PartitionOp> parsed;
    parsed.reserve(static_cast<std::size_t>(ops.size()));
    for (const auto& entry : ops) {
        const auto map  = entry.toMap();
        const auto kind = map.value(QStringLiteral("kind")).toString();

        planner::PartitionOp op;
        if (kind == QLatin1String("create")) {
            op.kind = Kind::Create;
        } else if (kind == QLatin1String("delete")) {
            op.kind = Kind::Delete;
        } else if (kind == QLatin1String("resize")) {
            op.kind = Kind::Resize;
        } else if (kind == QLatin1String("format")) {
            op.kind = Kind::Format;
        } else if (kind == QLatin1String("setflag")) {
            op.kind = Kind::SetFlag;
        } else if (kind == QLatin1String("setlabel")) {
            op.kind = Kind::SetLabel;
        } else if (kind == QLatin1String("newtable")) {
            op.kind = Kind::NewTable;
        } else {
            error = QStringLiteral("unknown partition op kind: %1").arg(kind);
            return {};
        }

        op.number      = map.value(QStringLiteral("number")).toUInt();
        op.start_bytes = map.value(QStringLiteral("startBytes")).toULongLong();
        op.end_bytes   = map.value(QStringLiteral("endBytes")).toULongLong();
        op.size_bytes  = map.value(QStringLiteral("sizeBytes")).toULongLong();
        op.fstype      = map.value(QStringLiteral("fstype")).toString().toStdString();
        op.label       = map.value(QStringLiteral("label")).toString().toStdString();
        op.flags       = map.value(QStringLiteral("flags")).toString().toStdString();
        op.flag_state  = map.value(QStringLiteral("flagState"), true).toBool();
        op.table_type  = map.value(QStringLiteral("tableType")).toString().toStdString();
        parsed.push_back(std::move(op));
    }
    return parsed;
}

// Parse the QML mount-row list into planner StagedMounts.
auto variantToStagedMounts(const QVariantList& mounts) -> std::vector<planner::StagedMount> {
    std::vector<planner::StagedMount> out;
    out.reserve(static_cast<std::size_t>(mounts.size()));
    for (const auto& entry : mounts) {
        const auto m = entry.toMap();
        out.push_back(planner::StagedMount{
            .created          = m.value(QStringLiteral("source")).toString() == QLatin1String("created"),
            .device           = m.value(QStringLiteral("device")).toString().toStdString(),
            .mountpoint       = m.value(QStringLiteral("mountpoint")).toString().toStdString(),
            .fstype           = m.value(QStringLiteral("fstype")).toString().toStdString(),
            .format_requested = m.value(QStringLiteral("formatRequested"), false).toBool(),
            .mount_opts       = m.value(QStringLiteral("mountOpts")).toString().toStdString(),
            .is_esp           = m.value(QStringLiteral("isEsp"), false).toBool(),
            .luks             = m.value(QStringLiteral("luks"), false).toBool(),
            .luks_passphrase  = m.value(QStringLiteral("luksPassphrase")).toString().toStdString(),
            .luks_mapper_name = m.value(QStringLiteral("luksMapperName")).toString().toStdString(),
            .luks_version     = m.value(QStringLiteral("luksVersion")).toString() == QLatin1String("Luks1")
                ? planner::LuksVersion::Luks1
                : planner::LuksVersion::Luks2,
        });
    }
    return out;
}

}  // namespace

void InstallerBackend::setPlannerError(QString error) {
    if (m_plannerError == error) {
        return;
    }
    m_plannerError = std::move(error);
    emit plannerErrorChanged();
}

QVariantMap InstallerBackend::discoverDisks() {
    setPlannerError({});
    const auto& inventory = planner::discover();

    QVariantList devices;
    devices.reserve(static_cast<qsizetype>(inventory.block_devices.size()));
    for (const auto& d : inventory.block_devices) {
        devices.append(deviceEntryToVariant(d));
    }
    QVariantList subvols;
    subvols.reserve(static_cast<qsizetype>(inventory.btrfs_subvolumes.size()));
    for (const auto& s : inventory.btrfs_subvolumes) {
        subvols.append(btrfsSubvolumeToVariant(s));
    }
    QVariantList pools;
    pools.reserve(static_cast<qsizetype>(inventory.zfs_pools.size()));
    for (const auto& p : inventory.zfs_pools) {
        pools.append(zfsPoolToVariant(p));
    }
    QVariantList groups;
    groups.reserve(static_cast<qsizetype>(inventory.lvm_groups.size()));
    for (const auto& g : inventory.lvm_groups) {
        groups.append(lvmGroupToVariant(g));
    }

    QVariantMap out;
    out.insert(QStringLiteral("blockDevices"), devices);
    out.insert(QStringLiteral("btrfsSubvolumes"), subvols);
    out.insert(QStringLiteral("zfsPools"), pools);
    out.insert(QStringLiteral("lvmGroups"), groups);
    return out;
}

QStringList InstallerBackend::inspectExistingBtrfs(const QString& device) {
    setPlannerError({});
    const auto& result = planner::inspect_existing_btrfs(device.toStdString());
    if (!result) {
        setPlannerError(QString::fromStdString(result.error()));
        return {};
    }
    QStringList out;
    out.reserve(static_cast<qsizetype>(result->size()));
    for (const auto& s : *result) {
        out.append(QString::fromStdString(s));
    }
    return out;
}

QVariantList InstallerBackend::freeSpaceRegions(const QString& device) {
    setPlannerError({});
    QVariantList out;
    for (const auto& region : planner::list_free_space(device.toStdString())) {
        QVariantMap row;
        row[QStringLiteral("startBytes")] = static_cast<qulonglong>(region.start_bytes);
        row[QStringLiteral("endBytes")]   = static_cast<qulonglong>(region.end_bytes);
        row[QStringLiteral("sizeBytes")]  = static_cast<qulonglong>(region.size_bytes);
        out.append(row);
    }
    return out;
}

QString InstallerBackend::createAlongsidePartition(const QString& device,
    qulonglong startBytes, qulonglong endBytes) {
    setPlannerError({});
    const auto result = planner::prepare_alongside_partition(device.toStdString(),
        static_cast<std::uint64_t>(startBytes), static_cast<std::uint64_t>(endBytes));
    if (!result) {
        setPlannerError(QString::fromStdString(result.error()));
        return {};
    }
    return QString::fromStdString(*result);
}

QVariantList InstallerBackend::partitionsForDevice(const QString& device) {
    setPlannerError({});
    QVariantList out;
    for (const auto& p : planner::list_partitions(device.toStdString())) {
        QVariantMap row;
        row[QStringLiteral("number")]     = static_cast<uint>(p.number);
        row[QStringLiteral("startBytes")] = static_cast<qulonglong>(p.start_bytes);
        row[QStringLiteral("endBytes")]   = static_cast<qulonglong>(p.end_bytes);
        row[QStringLiteral("sizeBytes")]  = static_cast<qulonglong>(p.size_bytes);
        row[QStringLiteral("device")]     = QString::fromStdString(p.device);
        row[QStringLiteral("fstype")]     = QString::fromStdString(p.fstype);
        row[QStringLiteral("name")]       = QString::fromStdString(p.name);
        row[QStringLiteral("flags")]      = QString::fromStdString(p.flags);
        row[QStringLiteral("partType")]   = QString::fromStdString(p.part_type);
        row[QStringLiteral("label")]      = QString::fromStdString(p.label);
        row[QStringLiteral("uuid")]       = QString::fromStdString(p.uuid);
        row[QStringLiteral("usedBytes")]  = static_cast<qulonglong>(p.used_bytes);
        out.append(row);
    }
    return out;
}

QVariantMap InstallerBackend::shrinkableBounds(const QString& device, uint number,
    const QString& fstype, qulonglong currentSizeBytes) {
    setPlannerError({});
    const auto res = planner::shrinkable_bounds(device.toStdString(), static_cast<std::uint32_t>(number),
        fstype.toStdString(), static_cast<std::uint64_t>(currentSizeBytes));
    QVariantMap out;
    if (!res) {
        setPlannerError(QString::fromStdString(res.error()));
        return out;
    }
    out[QStringLiteral("min")] = static_cast<qulonglong>(res->first);
    out[QStringLiteral("max")] = static_cast<qulonglong>(res->second);
    return out;
}

QString InstallerBackend::applyPartitionOps(const QString& device, const QVariantList& ops) {
    setPlannerError({});

    QString error;
    const auto parsed = variantToPartitionOps(ops, error);
    if (!error.isEmpty()) {
        setPlannerError(error);
        return {};
    }

    const auto result = planner::apply_partition_operations(device.toStdString(), parsed);
    if (!result) {
        setPlannerError(QString::fromStdString(result.error()));
        return {};
    }
    return QString::fromStdString(*result);
}

bool InstallerBackend::stagePartitionPlan(const QString& device,
    const QVariantList& ops, const QVariantList& mounts) {
    setPlannerError({});

    QString error;
    auto parsedOps = variantToPartitionOps(ops, error);
    if (!error.isEmpty()) {
        setPlannerError(error);
        return false;
    }
    auto parsedMounts = variantToStagedMounts(mounts);

    const auto roots = std::ranges::count_if(parsedMounts,
        [](const auto& m) { return m.mountpoint == "/"; });
    if (roots != 1) {
        setPlannerError(QStringLiteral("exactly one partition must be mounted at \"/\" (found %1)")
                .arg(static_cast<int>(roots)));
        return false;
    }

    m_stagedPartDevice = device;
    m_stagedPartOps    = std::move(parsedOps);
    m_stagedPartMounts = std::move(parsedMounts);
    // The actual MountSelections are produced from these at install time, on the
    // worker thread; clear any previously finalized plan so they don't conflict.
    clearMountSelections();
    return true;
}

bool InstallerBackend::stageZfsRoot(const QString& device, const QString& zpool,
    bool encrypt, const QString& passphrase) {
    setPlannerError({});
    if (device.isEmpty()) {
        setPlannerError(QStringLiteral("pick a target device for the ZFS install"));
        return false;
    }
    if (zpool.isEmpty()) {
        setPlannerError(QStringLiteral("enter a zpool name"));
        return false;
    }
    if (encrypt && passphrase.isEmpty()) {
        setPlannerError(QStringLiteral("enter a passphrase for ZFS encryption"));
        return false;
    }

    // A ZFS root owns the whole disk; drop any partition plan / finalized mounts.
    clearStagedPartitioning();
    clearMountSelections();
    m_stagedZfsActive     = true;
    m_stagedZfsDevice     = device;
    m_stagedZfsPool       = zpool;
    m_stagedZfsEncrypt    = encrypt;
    m_stagedZfsPassphrase = encrypt ? passphrase : QString{};
    return true;
}

void InstallerBackend::clearStagedPartitioning() {
    m_stagedPartDevice.clear();
    m_stagedPartOps.clear();
    m_stagedPartMounts.clear();
    m_stagedZfsActive = false;
    m_stagedZfsDevice.clear();
    m_stagedZfsPool.clear();
    m_stagedZfsEncrypt = false;
    m_stagedZfsPassphrase.clear();
}

QStringList InstallerBackend::stagedPartitionSummary() const {
    QStringList out;
    if (!hasStagedPartitioning()) {
        return out;
    }

    const auto gib = [](std::uint64_t bytes) {
        return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1)
            + QStringLiteral(" GiB");
    };

    using Kind = planner::PartitionOp::Kind;
    for (const auto& op : m_stagedPartOps) {
        switch (op.kind) {
        case Kind::NewTable:
            out << QStringLiteral("New %1 partition table on %2 (erases all existing partitions)")
                       .arg(QString::fromStdString(op.table_type).toUpper(), m_stagedPartDevice);
            break;
        case Kind::Delete:
            out << QStringLiteral("Delete partition #%1").arg(op.number);
            break;
        case Kind::Resize:
            out << QStringLiteral("Resize partition #%1 → %2").arg(op.number).arg(gib(op.size_bytes));
            break;
        case Kind::Create: {
            auto line = QStringLiteral("Create %1 partition").arg(gib(op.end_bytes - op.start_bytes + 1));
            if (!op.fstype.empty()) {
                line += QStringLiteral(" (%1)").arg(QString::fromStdString(op.fstype));
            }
            if (!op.flags.empty()) {
                line += QStringLiteral(" [%1]").arg(QString::fromStdString(op.flags));
            }
            out << line;
            break;
        }
        case Kind::Format:
            out << QStringLiteral("Format partition #%1 as %2")
                       .arg(op.number)
                       .arg(QString::fromStdString(op.fstype));
            break;
        case Kind::SetFlag:
            out << QStringLiteral("Set flag %1 %2 on partition #%3")
                       .arg(QString::fromStdString(op.flags),
                           op.flag_state ? QStringLiteral("on") : QStringLiteral("off"))
                       .arg(op.number);
            break;
        case Kind::SetLabel:
            out << QStringLiteral("Label partition #%1 \"%2\"")
                       .arg(op.number)
                       .arg(QString::fromStdString(op.label));
            break;
        }
    }

    for (const auto& m : m_stagedPartMounts) {
        const auto where = m.created
            ? QStringLiteral("new partition")
            : QString::fromStdString(m.device);
        auto line        = QStringLiteral("Mount %1 at %2 (%3%4)")
                               .arg(where,
                                   QString::fromStdString(m.mountpoint),
                                   QString::fromStdString(m.fstype),
                                   m.format_requested ? QStringLiteral(", format") : QString{});
        out << line;
    }
    return out;
}

QVariantList InstallerBackend::defaultBtrfsLayout() const {
    const auto& layout = planner::default_btrfs_layout();
    QVariantList out;
    out.reserve(static_cast<qsizetype>(layout.size()));
    for (const auto& c : layout) {
        out.append(btrfsChoiceToVariant(c));
    }
    return out;
}

QVariantList InstallerBackend::defaultZfsLayout(const QString& zpool) const {
    const auto& layout = planner::default_zfs_layout(zpool.toStdString());
    QVariantList out;
    out.reserve(static_cast<qsizetype>(layout.size()));
    for (const auto& c : layout) {
        out.append(zfsChoiceToVariant(c));
    }
    return out;
}

QString InstallerBackend::suggestMountpointForSubvolume(const QString& subvol) const {
    return QString::fromStdString(planner::suggest_mountpoint_for_subvolume(subvol.toStdString()));
}

bool InstallerBackend::prepareZpool(const QString& partition, const QString& zpool, const QString& mountpoint) {
    setPlannerError({});
    const auto& result = planner::prepare_default_zpool(
        partition.toStdString(), zpool.toStdString(), mountpoint.toStdString());
    if (!result) {
        setPlannerError(QString::fromStdString(result.error()));
        return false;
    }
    return true;
}

bool InstallerBackend::encryptPartition(const QString& device, const QString& mapperName,
    const QString& passphrase, const QString& extraFlags, const QString& luksVersion) {
    setPlannerError({});
    const planner::LuksFormatRequest req{
        .device      = device.toStdString(),
        .mapper_name = mapperName.toStdString(),
        .passphrase  = passphrase.toStdString(),
        .extra_flags = extraFlags.toStdString(),
        .version     = luksVersion == QStringLiteral("Luks1")
            ? planner::LuksVersion::Luks1
            : planner::LuksVersion::Luks2,
    };
    const auto& result = planner::encrypt_partition(req);
    if (!result) {
        setPlannerError(QString::fromStdString(result.error()));
        return false;
    }
    return true;
}

bool InstallerBackend::openEncryptedPartition(const QString& device, const QString& mapperName,
    const QString& passphrase) {
    setPlannerError({});
    const planner::LuksOpenRequest req{
        .device      = device.toStdString(),
        .mapper_name = mapperName.toStdString(),
        .passphrase  = passphrase.toStdString(),
    };
    const auto& result = planner::open_encrypted_partition(req);
    if (!result) {
        setPlannerError(QString::fromStdString(result.error()));
        return false;
    }
    return true;
}

bool InstallerBackend::finalizePlan(const QVariantMap& plan) {
    setPlannerError({});

    planner::PartitionPlan p{};
    p.root = variantToRoot(plan.value(QStringLiteral("root")).toMap());
    p.swap = variantToSwap(plan.value(QStringLiteral("swap")).toMap());
    p.esp  = variantToEsp(plan.value(QStringLiteral("esp")).toMap());

    const auto& additional = plan.value(QStringLiteral("additional")).toList();
    p.additional.reserve(static_cast<std::size_t>(additional.size()));
    for (const auto& entry : additional) {
        p.additional.push_back(variantToAdditional(entry.toMap()));
    }

    const auto& subvols = plan.value(QStringLiteral("btrfsSubvolumes")).toList();
    p.btrfs_subvolumes.reserve(static_cast<std::size_t>(subvols.size()));
    for (const auto& entry : subvols) {
        p.btrfs_subvolumes.push_back(variantToBtrfsChoice(entry.toMap()));
    }

    auto result = planner::finalize_plan(std::move(p));
    if (!result) {
        setPlannerError(QString::fromStdString(result.error()));
        if (m_mountSelections.has_value()) {
            m_mountSelections.reset();
            emit mountSelectionsChanged();
        }
        return false;
    }

    m_mountSelections = std::move(*result);
    emit mountSelectionsChanged();
    return true;
}

void InstallerBackend::clearMountSelections() {
    if (!m_mountSelections.has_value()) {
        return;
    }
    m_mountSelections.reset();
    emit mountSelectionsChanged();
}
