import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller
import "partition"
import "../components/Format.js" as Fmt

Item {
    id: root

    // One of: "Erase" | "Replace" | "Alongside" | "Manual" — set by ModeSelector.
    // "Erase", "Replace" and "Manual" are wired end-to-end through buildPlan().
    // "Alongside" installs into existing unallocated free space (no shrinking).
    property string partitionMode: "Erase"

    // Surfaces the user's final choices to SummaryPage / WizardNavBar.
    property string selectedDevice: ""
    property string selectedFs: ""
    property string selectedReplacePartition: ""
    // Alongside mode: the shrink+create op list emitted by AlongsideEditor.
    property var alongsideOps: []
    // Manual mode: the op list + mount rows emitted by ManualEditor.
    property var manualOps: []
    property var manualMounts: []
    property bool   useDefaultLayout: true
    property bool   encryptRoot: false
    property string luksPassphrase: luksPassphraseField.text
    property string swapType: "None"      // "None" | "Swapfile"
    property string swapfileSize: "4G"
    // Selected bootloader (bound from main.qml). Gates ZFS native encryption,
    // which GRUB cannot read.
    property string selectedBootloader: ""
    // ZFS root (Erase mode, selectedFs === "zfs"): whole-disk pool created at install.
    property string zfsPoolName: "zpcachyos"
    property bool   zfsEncrypt: false
    property string zfsPassphrase: zfsPassphraseField.text

    // Full DiskInventory as a JS object (block devices, btrfs subvols, zfs pools, lvm groups).
    property var inventory: ({ blockDevices: [], btrfsSubvolumes: [], zfsPools: [], lvmGroups: [] })
    property string statusMessage: ""

    function deviceComboModel() {
        var result = [];
        for (var i = 0; i < root.inventory.blockDevices.length; ++i) {
            var d = root.inventory.blockDevices[i];
            // Only show top-level disks (no parent) so the user picks a disk, not a partition.
            if (d.parent && d.parent !== "")
                continue;
            var label = d.name + "  (" + Fmt.formatSize(d.sizeBytes)
                      + (d.model ? ", " + d.model : "")
                      + ")";
            result.push({key: d.name, name: label});
        }
        return result;
    }

    function refreshInventory() {
        root.inventory = InstallerBackend.discoverDisks();
        if (root.selectedDevice === "") {
            var model = root.deviceComboModel();
            if (model.length > 0)
                root.selectedDevice = model[0].key;
        }
    }

    function buildPlan() {
        // Re-derive from scratch each validation so a previously staged plan can
        // never leak into another mode/disk.
        InstallerBackend.clearStagedPartitioning();
        if (root.partitionMode === "Manual") {
            var mounts = root.manualMounts || [];
            var ops = root.manualOps || [];
            var createdMounts = mounts.filter(function(m) { return m.source === "created"; });
            if (createdMounts.length > 1) {
                root.statusMessage = "Only one newly-created partition can be assigned a mountpoint.";
                return false;
            }
            var rootMounts = mounts.filter(function(m) { return m.mountpoint === "/"; });
            if (rootMounts.length !== 1) {
                root.statusMessage = "Assign exactly one partition to mount at \"/\".";
                return false;
            }

            // No destructive ops and nothing new: mount existing partitions
            // straight away (read-only, works without root).
            if (ops.length === 0 && createdMounts.length === 0) {
                var rootRow = null;
                var espRow = null;
                var additional = [];
                for (var i = 0; i < mounts.length; ++i) {
                    var m = mounts[i];
                    var row = {
                        device:          m.device,
                        mountpoint:      m.mountpoint,
                        fstype:          m.fstype,
                        mkfsCommand:     "",
                        mountOpts:       m.mountOpts || "",
                        formatRequested: !!m.formatRequested,
                    };
                    if (m.mountpoint === "/") rootRow = row;
                    else if (m.isEsp) espRow = row;
                    else additional.push(row);
                }
                var manualPlan = {
                    root: {
                        device:          rootRow.device,
                        fstype:          rootRow.fstype,
                        mkfsCommand:     "",
                        mountOpts:       rootRow.mountOpts || "",
                        formatRequested: !!rootRow.formatRequested,
                    },
                    swap: { type: "None", device: "", swapfileSize: "", needsMkswap: false },
                    esp: espRow
                        ? { device: espRow.device, mountpoint: espRow.mountpoint, formatRequested: !!espRow.formatRequested }
                        : { device: "", mountpoint: "", formatRequested: false },
                    additional: additional,
                    btrfsSubvolumes: rootRow.fstype === "btrfs" ? InstallerBackend.defaultBtrfsLayout() : [],
                };
                var mok = InstallerBackend.finalizePlan(manualPlan);
                if (!mok) {
                    root.statusMessage = "Plan invalid: " + InstallerBackend.plannerError;
                    return false;
                }
                root.statusMessage = "Plan accepted: " + additional.length + " extra mount(s), root on " + rootRow.device + ".";
                return true;
            }

            // Destructive edits: stage them now (read-only) — they are applied at
            // install time, as root, on the worker thread.
            var staged = InstallerBackend.stagePartitionPlan(root.selectedDevice, ops, mounts);
            if (!staged) {
                root.statusMessage = "Plan invalid: " + InstallerBackend.plannerError;
                return false;
            }
            root.statusMessage = "Plan validated. " + ops.length
                + " partition change(s) will be applied during installation.";
            return true;
        }
        if (root.partitionMode === "Replace") {
            if (root.selectedReplacePartition === "") {
                root.statusMessage = "Pick the partition to format as root.";
                return false;
            }
            var replacePlan = {
                root: {
                    device:          root.selectedReplacePartition,
                    fstype:          root.selectedFs,
                    mkfsCommand:     "",
                    mountOpts:       "",
                    formatRequested: true,
                },
                swap: { type: "None", device: "", swapfileSize: "", needsMkswap: false },
                esp: { device: "", mountpoint: "", formatRequested: false },
                additional: [],
                btrfsSubvolumes: [],
            };
            if (root.selectedFs === "btrfs") {
                replacePlan.btrfsSubvolumes = InstallerBackend.defaultBtrfsLayout();
            }
            var rok = InstallerBackend.finalizePlan(replacePlan);
            if (!rok) {
                root.statusMessage = "Plan invalid: " + InstallerBackend.plannerError;
                return false;
            }
            root.statusMessage = "Plan accepted: " + root.selectedReplacePartition + " will be formatted as root.";
            return true;
        }
        if (root.partitionMode === "Alongside") {
            if (!root.alongsideOps || root.alongsideOps.length === 0) {
                root.statusMessage = "Pick a partition and drag the splitter to free space for the install.";
                return false;
            }
            // Shrink + create are staged (read-only); the new partition becomes the
            // root mount, formatted with the chosen filesystem. Applied at install.
            var alongsideMounts = [{
                source:          "created",
                mountpoint:      "/",
                fstype:          root.selectedFs,
                formatRequested: true,
                mountOpts:       "",
            }];
            var astaged = InstallerBackend.stagePartitionPlan(
                root.selectedDevice, root.alongsideOps, alongsideMounts);
            if (!astaged) {
                root.statusMessage = "Plan invalid: " + InstallerBackend.plannerError;
                return false;
            }
            root.statusMessage = "Plan validated. A partition will be shrunk and a new root created during installation.";
            return true;
        }
        if (root.partitionMode !== "Erase") {
            root.statusMessage = root.partitionMode + " mode is not implemented yet — pick \"Erase disk\" to proceed.";
            return false;
        }
        // ZFS root: the whole disk becomes a zpool (created on the worker thread at
        // install time, where the bootloader — and thus ESP + encryption rules — is
        // known). Nothing is written now.
        if (root.selectedFs === "zfs") {
            var zpoolName = root.zfsPoolName.length > 0 ? root.zfsPoolName : "zpcachyos";
            if (root.zfsEncrypt && root.zfsPassphrase.length === 0) {
                root.statusMessage = "Enter a ZFS encryption passphrase.";
                return false;
            }
            var zok = InstallerBackend.stageZfsRoot(
                root.selectedDevice, zpoolName, root.zfsEncrypt, root.zfsPassphrase);
            if (!zok) {
                root.statusMessage = "ZFS plan invalid: " + InstallerBackend.plannerError;
                return false;
            }
            root.statusMessage = "ZFS root staged on zpool \"" + zpoolName + "\" — created during installation.";
            return true;
        }
        // Wizard mode (default layout, whole disk): leave mount_selections empty so
        // the orchestrator's auto_partition path runs.
        if (root.useDefaultLayout) {
            if (root.encryptRoot && root.luksPassphrase.length === 0) {
                root.statusMessage = "LUKS passphrase is required.";
                return false;
            }
            InstallerBackend.clearMountSelections();
            root.statusMessage = "Using default partition layout (auto-partitioning will run during install)."
                + (root.encryptRoot ? " Root will be LUKS-encrypted." : "");
            return true;
        }

        // Advanced mode: hand the planner an explicit plan over the already-prepared
        // root partition. The user is expected to have created /dev/<device>1
        // (or similar) outside this wizard for now.
        var rootDevice = root.encryptRoot
            ? "/dev/mapper/cryptroot"
            : root.selectedDevice;

        if (root.encryptRoot) {
            if (root.luksPassphrase.length === 0) {
                root.statusMessage = "LUKS passphrase is required.";
                return false;
            }
            var ok = InstallerBackend.encryptPartition(
                root.selectedDevice, "cryptroot", root.luksPassphrase, "");
            if (!ok) {
                root.statusMessage = "Encryption failed: " + InstallerBackend.plannerError;
                return false;
            }
        }

        var plan = {
            root: {
                device:          rootDevice,
                fstype:          root.selectedFs,
                mkfsCommand:     "",
                mountOpts:       "",
                formatRequested: true,
            },
            swap: {
                type:         root.swapType,
                device:       "",
                swapfileSize: root.swapfileSize,
                needsMkswap:  root.swapType === "Swapfile",
            },
            esp: InstallerBackend.isUefi
                ? { device: "", mountpoint: "/boot/efi", formatRequested: true }
                : { device: "", mountpoint: "", formatRequested: false },
            additional: [],
            btrfsSubvolumes: [],
        };

        if (root.selectedFs === "btrfs") {
            plan.btrfsSubvolumes = InstallerBackend.defaultBtrfsLayout();
        }

        var finalized = InstallerBackend.finalizePlan(plan);
        if (!finalized) {
            root.statusMessage = "Plan invalid: " + InstallerBackend.plannerError;
            return false;
        }
        root.statusMessage = "Plan accepted.";
        return true;
    }

    // grub can't read native-encrypted ZFS — drop the choice if the bootloader
    // changes to grub so a stale enabled state can't be staged.
    onSelectedBootloaderChanged: {
        if (root.selectedBootloader === "grub")
            root.zfsEncrypt = false;
    }

    Connections {
        target: InstallerBackend
        function onListsPopulated() {
            if (root.selectedFs === "" && InstallerBackend.filesystemList.length > 0)
                root.selectedFs = InstallerBackend.filesystemList[0];
        }
    }

    Component.onCompleted: refreshInventory()

    ScrollView {
        anchors.fill: parent
        clip: true

        ColumnLayout {
            width: root.width
            anchors.margins: Style.marginXL
            spacing: Style.marginL

            PageTitle {
                Layout.fillWidth: true
                title: "Disk Partitioning"
                subtitle: "Pick how you want the installer to lay out partitions on the target disk."
            }

            ModeSelector {
                Layout.fillWidth: true
                mode: root.partitionMode
                onModeChanged: {
                    root.partitionMode = mode;
                    // A staged plan belongs to the mode that built it.
                    InstallerBackend.clearStagedPartitioning();
                }
            }

            // Target disk picker — shown for Erase, Replace, Manual, and Alongside.
            NComboBox {
                visible: root.partitionMode === "Erase" || root.partitionMode === "Replace"
                         || root.partitionMode === "Manual" || root.partitionMode === "Alongside"
                Layout.fillWidth: true
                label: "Target Device"
                model: root.deviceComboModel()
                currentKey: root.selectedDevice
                onSelected: function(key) {
                    root.selectedDevice = key;
                    // Reset the per-mode picks whenever the parent disk changes.
                    root.selectedReplacePartition = "";
                    root.alongsideOps = [];
                    InstallerBackend.clearStagedPartitioning();
                }
            }

            // Alongside mode: shrink an existing partition with a draggable splitter.
            AlongsideEditor {
                Layout.fillWidth: true
                visible: root.partitionMode === "Alongside"
                parentDevice: root.selectedDevice
                blockDevices: root.inventory.blockDevices
                onOpsChanged: root.alongsideOps = ops
            }

            // Replace mode: pick which existing partition to format as root.
            ReplacePicker {
                Layout.fillWidth: true
                visible: root.partitionMode === "Replace"
                blockDevices: root.inventory.blockDevices
                parentDevice: root.selectedDevice
                selectedPartition: root.selectedReplacePartition
                onSelectedPartitionChanged: root.selectedReplacePartition = selectedPartition
            }

            // Manual mode: full editor (bar + table + create/edit/delete ops).
            ManualEditor {
                Layout.fillWidth: true
                visible: root.partitionMode === "Manual"
                blockDevices: root.inventory.blockDevices
                parentDevice: root.selectedDevice
                onOpsChanged: root.manualOps = ops
                onMountsChanged: root.manualMounts = mounts
            }

            NComboBox {
                visible: root.partitionMode === "Erase" || root.partitionMode === "Replace"
                         || root.partitionMode === "Alongside"
                Layout.fillWidth: true
                label: "Filesystem"
                model: Fmt.toComboModel(InstallerBackend.filesystemList)
                currentKey: root.selectedFs
                onSelected: function(key) { root.selectedFs = key; }
            }

            NCheckbox {
                visible: root.partitionMode === "Erase" && root.selectedFs !== "zfs"
                Layout.fillWidth: true
                label: "Use default partition layout (recommended)"
                description: "Wipes the disk and lets the installer create root, swap, and ESP."
                checked: root.useDefaultLayout
                onToggled: function(c) { root.useDefaultLayout = c; }
            }

            // ZFS root options.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.marginM
                visible: root.partitionMode === "Erase" && root.selectedFs === "zfs"

                NTextInput {
                    Layout.fillWidth: true
                    label: "ZFS pool name"
                    text: root.zfsPoolName
                    onEditingFinished: root.zfsPoolName = text
                }

                // GRUB cannot read a natively-encrypted pool, so the option is
                // hidden under grub.
                NCheckbox {
                    Layout.fillWidth: true
                    visible: root.selectedBootloader !== "grub"
                    label: "Encrypt with ZFS native encryption"
                    checked: root.zfsEncrypt
                    onToggled: function(c) { root.zfsEncrypt = c; }
                }

                PasswordField {
                    id: zfsPassphraseField
                    Layout.fillWidth: true
                    label: "ZFS passphrase"
                    visible: root.zfsEncrypt && root.selectedBootloader !== "grub"
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: {
                        var pool = root.zfsPoolName.length > 0 ? root.zfsPoolName : "zpcachyos";
                        var ds = InstallerBackend.defaultZfsLayout(pool);
                        var lines = ["Datasets:"];
                        for (var i = 0; i < ds.length; ++i)
                            lines.push("  " + ds[i].dataset
                                       + (ds[i].mountpoint !== "none" ? "  →  " + ds[i].mountpoint : ""));
                        return lines.join("\n");
                    }
                    font.pixelSize: Style.fontSizeS
                    color: Color.mOnSurfaceVariant
                }
            }

            // Encryption
            // (LUKS full-disk; for zfs use the native-encryption option above).
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.marginM
                visible: root.partitionMode === "Erase" && root.selectedFs !== "zfs"

                NCheckbox {
                    Layout.fillWidth: true
                    label: "Encrypt root with LUKS"
                    description: "Full-disk encryption of the root partition (you'll be asked for the passphrase at boot)."
                    checked: root.encryptRoot
                    onToggled: function(c) { root.encryptRoot = c; }
                }

                PasswordField {
                    id: luksPassphraseField
                    Layout.fillWidth: true
                    label: "LUKS passphrase"
                    visible: root.encryptRoot
                }
            }

            // Advanced section
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.marginM
                visible: root.partitionMode === "Erase" && !root.useDefaultLayout && root.selectedFs !== "zfs"

                NComboBox {
                    Layout.fillWidth: true
                    label: "Swap"
                    model: [
                        { key: "None",     name: "No swap" },
                        { key: "Swapfile", name: "Swapfile" },
                    ]
                    currentKey: root.swapType
                    onSelected: function(key) { root.swapType = key; }
                }

                NTextInput {
                    Layout.fillWidth: true
                    label: "Swapfile size"
                    visible: root.swapType === "Swapfile"
                    text: root.swapfileSize
                    onEditingFinished: root.swapfileSize = text
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "Btrfs layout: CachyOS canonical subvolumes will be used."
                    visible: root.selectedFs === "btrfs"
                    font.pixelSize: Style.fontSizeS
                    color: Color.mOnSurfaceVariant
                }
            }

            // Warning — copy adapts to the active mode.
            Rectangle {
                visible: (root.partitionMode === "Erase" && (root.useDefaultLayout || root.selectedFs === "zfs"))
                         || (root.partitionMode === "Replace" && root.selectedReplacePartition !== "")
                Layout.fillWidth: true
                height: warningText.implicitHeight + Style.margin2L
                radius: Style.radiusS
                color: Qt.alpha(Color.mError, 0.15)
                border.color: Color.mError
                border.width: Style.borderS

                Text {
                    id: warningText
                    anchors.centerIn: parent
                    width: parent.width - Style.margin2L
                    text: root.partitionMode === "Replace"
                        ? ("Warning: all data on " + root.selectedReplacePartition + " will be permanently erased. Other partitions on the disk are left alone.")
                        : "Warning: All data on the selected device will be permanently erased!"
                    font.pixelSize: Style.fontSizeM
                    font.weight: Style.fontWeightSemiBold
                    color: Color.mError
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // Plan status
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: root.statusMessage
                visible: root.statusMessage !== ""
                font.pixelSize: Style.fontSizeS
                color: InstallerBackend.plannerError === "" ? Color.mOnSurfaceVariant : Color.mError
            }

            NButton {
                text: "Validate plan"
                onClicked: root.buildPlan()
            }

            Item { Layout.fillHeight: true }
        }
    }
}
