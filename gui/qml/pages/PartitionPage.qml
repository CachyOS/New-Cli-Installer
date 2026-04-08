import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller
import "partition"

Item {
    id: root

    // One of: "Erase" | "Replace" | "Alongside" | "Manual" — set by ModeSelector.
    // "Erase", "Replace" and "Manual" are wired end-to-end through buildPlan().
    // "Alongside" is still a placeholder (needs free-space/shrink support in the planner).
    property string partitionMode: "Erase"

    // Surfaces the user's final choices to SummaryPage / WizardNavBar.
    property string selectedDevice: ""
    property string selectedFs: ""
    property string selectedReplacePartition: ""
    property var manualAssignments: []
    property bool   useDefaultLayout: true
    property bool   encryptRoot: false
    property string luksPassphrase: ""
    property string luksVersion: "Luks2"
    property string swapType: "None"      // "None" | "Swapfile"
    property string swapfileSize: "4G"

    // Full DiskInventory as a JS object (block devices, btrfs subvols, zfs pools, lvm groups).
    property var inventory: ({ blockDevices: [], btrfsSubvolumes: [], zfsPools: [], lvmGroups: [] })
    property string statusMessage: ""

    function toComboModel(list) {
        var result = [];
        for (var i = 0; i < list.length; ++i)
            result.push({key: list[i], name: list[i]});
        return result;
    }

    function deviceComboModel() {
        var result = [];
        for (var i = 0; i < root.inventory.blockDevices.length; ++i) {
            var d = root.inventory.blockDevices[i];
            // Only show top-level disks (no parent) so the user picks a disk, not a partition.
            if (d.parent && d.parent !== "")
                continue;
            var sizeGB = (d.sizeBytes / (1024 * 1024 * 1024)).toFixed(1);
            var label = d.name + "  (" + sizeGB + " GiB"
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
        if (root.partitionMode === "Manual") {
            var rootRow = null;
            var espRow = null;
            var additional = [];
            for (var i = 0; i < root.manualAssignments.length; ++i) {
                var a = root.manualAssignments[i];
                if (!a.mountpoint || a.mountpoint === "")
                    continue;
                if (a.mountpoint === "/") {
                    rootRow = a;
                } else if (a.mountpoint === "/boot/efi") {
                    espRow = a;
                } else {
                    additional.push({
                        device:          a.device,
                        mountpoint:      a.mountpoint,
                        fstype:          a.fstype,
                        mkfsCommand:     "",
                        mountOpts:       a.mountOpts || "",
                        formatRequested: !!a.formatRequested,
                    });
                }
            }
            if (!rootRow) {
                root.statusMessage = "Pick exactly one partition to mount at \"/\".";
                return false;
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
                    ? { device: espRow.device, mountpoint: "/boot/efi", formatRequested: !!espRow.formatRequested }
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
        if (root.partitionMode !== "Erase") {
            root.statusMessage = root.partitionMode + " mode is not implemented yet — pick \"Erase disk\" to proceed.";
            return false;
        }
        // Wizard mode (default layout, whole disk): leave mount_selections empty so
        // the orchestrator's auto_partition path runs.
        if (root.useDefaultLayout) {
            InstallerBackend.clearMountSelections();
            root.statusMessage = "Using default partition layout (auto-partitioning will run during install).";
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
                root.selectedDevice, "cryptroot", root.luksPassphrase, "", root.luksVersion);
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

            Text {
                text: "Disk Partitioning"
                font.pixelSize: Style.fontSizeXXL
                font.weight: Style.fontWeightBold
                color: Color.mOnSurface
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "Pick how you want the installer to lay out partitions on the target disk."
                font.pixelSize: Style.fontSizeM
                color: Color.mOnSurfaceVariant
            }

            ModeSelector {
                Layout.fillWidth: true
                mode: root.partitionMode
                onModeChanged: root.partitionMode = mode
            }

            // Placeholder for modes whose editors aren't built yet.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: placeholderText.implicitHeight + Style.margin2L
                visible: root.partitionMode === "Alongside"
                radius: Style.radiusS
                color: Qt.alpha(Color.mPrimary, 0.10)
                border.color: Color.mPrimary
                border.width: Style.borderS
                Text {
                    id: placeholderText
                    anchors.centerIn: parent
                    width: parent.width - Style.margin2L
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    text: "Alongside mode is coming soon. Pick another mode to proceed for now."
                    font.pixelSize: Style.fontSizeM
                    color: Color.mPrimary
                }
            }

            // Target disk picker — shown for Erase, Replace, and Manual.
            NComboBox {
                visible: root.partitionMode === "Erase" || root.partitionMode === "Replace" || root.partitionMode === "Manual"
                Layout.fillWidth: true
                label: "Target Device"
                model: root.deviceComboModel()
                currentKey: root.selectedDevice
                onSelected: function(key) {
                    root.selectedDevice = key;
                    // Reset the per-mode picks whenever the parent disk changes.
                    root.selectedReplacePartition = "";
                }
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

            // Manual mode: per-partition mountpoint/fs/format/mountopts table.
            ManualEditor {
                Layout.fillWidth: true
                visible: root.partitionMode === "Manual"
                blockDevices: root.inventory.blockDevices
                parentDevice: root.selectedDevice
                onAssignmentsChanged: root.manualAssignments = assignments
            }

            NComboBox {
                visible: root.partitionMode === "Erase" || root.partitionMode === "Replace"
                Layout.fillWidth: true
                label: "Filesystem"
                model: root.toComboModel(InstallerBackend.filesystemList)
                currentKey: root.selectedFs
                onSelected: function(key) { root.selectedFs = key; }
            }

            NCheckbox {
                visible: root.partitionMode === "Erase"
                Layout.fillWidth: true
                label: "Use default partition layout (recommended)"
                description: "Wipes the disk and lets the installer create root, swap, and ESP."
                checked: root.useDefaultLayout
                onToggled: function(c) { root.useDefaultLayout = c; }
            }

            // Advanced section
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Style.marginM
                visible: root.partitionMode === "Erase" && !root.useDefaultLayout

                NCheckbox {
                    Layout.fillWidth: true
                    label: "Encrypt root with LUKS"
                    checked: root.encryptRoot
                    onToggled: function(c) { root.encryptRoot = c; }
                }

                NTextInput {
                    Layout.fillWidth: true
                    label: "LUKS passphrase"
                    visible: root.encryptRoot
                    showClearButton: false
                    Component.onCompleted: {
                        inputItem.echoMode = TextInput.Password;
                        root.luksPassphrase = Qt.binding(function() { return text; });
                    }
                }

                NComboBox {
                    Layout.fillWidth: true
                    label: "LUKS version"
                    visible: root.encryptRoot
                    model: [
                        { key: "Luks2", name: "LUKS2 (recommended — modern, Argon2 KDF)" },
                        { key: "Luks1", name: "LUKS1 (legacy — for older GRUB on /boot)" },
                    ]
                    currentKey: root.luksVersion
                    onSelected: function(key) { root.luksVersion = key; }
                }

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
                visible: (root.partitionMode === "Erase" && root.useDefaultLayout)
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
