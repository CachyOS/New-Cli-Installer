import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Item {
    id: root

    // Surfaces the user's final choices to SummaryPage / WizardNavBar.
    property string selectedDevice: ""
    property string selectedFs: ""
    property bool   useDefaultLayout: true
    property bool   encryptRoot: false
    property string luksPassphrase: ""
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
                text: "Pick the target disk and the filesystem you want. The default layout " +
                      "erases the disk and lets the installer partition it for you. Switch " +
                      "to advanced layout if you want LUKS-encrypted root or a custom swap."
                font.pixelSize: Style.fontSizeM
                color: Color.mOnSurfaceVariant
            }

            NComboBox {
                Layout.fillWidth: true
                label: "Target Device"
                model: root.deviceComboModel()
                currentKey: root.selectedDevice
                onSelected: function(key) { root.selectedDevice = key; }
            }

            NComboBox {
                Layout.fillWidth: true
                label: "Filesystem"
                model: root.toComboModel(InstallerBackend.filesystemList)
                currentKey: root.selectedFs
                onSelected: function(key) { root.selectedFs = key; }
            }

            NCheckbox {
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
                visible: !root.useDefaultLayout

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

            // Warning
            Rectangle {
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
                    text: "Warning: All data on the selected device will be permanently erased!"
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
