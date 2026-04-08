#include "InstallerBackend.hpp"
#include "InstallWorker.hpp"

// import instlib
#include "cachyos/disk.hpp"
#include "cachyos/installer_data.hpp"
#include "cachyos/partition_planner.hpp"
#include "cachyos/system.hpp"

// import gucc
#include "gucc/bootloader.hpp"
#include "gucc/locale.hpp"
#include "gucc/timezone.hpp"

#include <string_view>  // for string_view
#include <utility>      // for move

#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

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
  : QObject(parent), m_isRoot(cachyos::installer::check_root()) {
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
    const auto& bios_mode   = m_isUefi ? "UEFI"sv : "BIOS"sv;
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

QStringList InstallerBackend::getAvailableMountOpts(const QString& fstype) const {
    const auto& opts = cachyos::installer::get_available_mount_opts(fstype.toStdString());
    return toQStringList(opts);
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
    return m_currentPage < pageCount() - 1 && !m_isInstalling;
}

bool InstallerBackend::canGoBack() const {
    return m_currentPage > 0 && !m_isInstalling;
}

void InstallerBackend::startInstallation(const QVariantMap& selections) {
    if (m_isInstalling) {
        return;
    }

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

    if (m_mountSelections.has_value()) {
        ctx.mount_selections = m_mountSelections;
    }

    if (selections.contains(QStringLiteral("carryLiveNetwork"))) {
        ctx.carry_live_network = selections.value(QStringLiteral("carryLiveNetwork")).toBool();
    }
    if (selections.contains(QStringLiteral("installChwdProfiles"))) {
        ctx.install_chwd_profiles = selections.value(QStringLiteral("installChwdProfiles")).toBool();
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
    if (m_worker == nullptr) {
        return;
    }
    spdlog::info("Installation cancellation requested");
    m_worker->cancel();
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
    using Type = cachyos::installer::SwapSelection::Type;
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
    const QString& passphrase, const QString& extraFlags) {
    setPlannerError({});
    const planner::LuksFormatRequest req{
        .device      = device.toStdString(),
        .mapper_name = mapperName.toStdString(),
        .passphrase  = passphrase.toStdString(),
        .extra_flags = extraFlags.toStdString(),
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
