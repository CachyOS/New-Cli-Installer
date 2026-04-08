import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

ColumnLayout {
    id: root

    // Full block-device list (inventory.blockDevices). Rows are built from the
    // subset whose `parent` matches @ref parentDevice.
    property var blockDevices: []
    // Disk the user picked above (e.g. "/dev/sda").
    property string parentDevice: ""

    // Editable per-row state, indexed parallel to @ref candidatePartitions.
    // Each entry: { device, mountpoint, fstype, formatRequested, mountOpts }.
    // PartitionPage reads this to assemble the PartitionPlan.
    property var assignments: []

    readonly property var candidatePartitions: {
        var out = [];
        for (var i = 0; i < blockDevices.length; ++i) {
            var d = blockDevices[i];
            if (!d.parent || d.parent === "")
                continue;
            if (parentDevice !== "" && d.parent !== parentDevice)
                continue;
            out.push(d);
        }
        return out;
    }

    function formatSize(bytes) {
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB";
    }

    function _initialFs(detected) {
        // Prefer the existing fstype when it's one we recognize; fall back to ext4.
        var known = ["ext4", "btrfs", "xfs", "vfat"];
        return known.indexOf(detected) !== -1 ? detected : "ext4";
    }

    function rebuildAssignments() {
        var next = [];
        var parts = candidatePartitions;
        for (var i = 0; i < parts.length; ++i) {
            var d = parts[i];
            next.push({
                device:          d.name,
                mountpoint:      "",
                fstype:          _initialFs(d.fstype),
                formatRequested: false,
                mountOpts:       "",
            });
        }
        root.assignments = next;
    }

    onParentDeviceChanged: rebuildAssignments()
    onCandidatePartitionsChanged: rebuildAssignments()
    Component.onCompleted: rebuildAssignments()

    spacing: Style.marginS

    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: candidatePartitions.length === 0
              ? "No partitions found on " + (parentDevice || "the selected disk") + ". Manual mode requires pre-existing partitions on the target disk."
              : "Assign a mountpoint to each partition you want to use. Leave the mountpoint blank to skip a partition. Exactly one row must be mounted at \"/\"."
        font.pixelSize: Style.fontSizeM
        color: Color.mOnSurfaceVariant
    }

    // Column headers
    RowLayout {
        Layout.fillWidth: true
        visible: candidatePartitions.length > 0
        spacing: Style.marginS

        Text { text: "Device";     Layout.preferredWidth: 130; font.pixelSize: Style.fontSizeS; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurfaceVariant }
        Text { text: "Mountpoint"; Layout.fillWidth: true;     font.pixelSize: Style.fontSizeS; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurfaceVariant }
        Text { text: "Filesystem"; Layout.preferredWidth: 110; font.pixelSize: Style.fontSizeS; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurfaceVariant }
        Text { text: "Format";     Layout.preferredWidth: 70;  font.pixelSize: Style.fontSizeS; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurfaceVariant; horizontalAlignment: Text.AlignHCenter }
        Text { text: "Mount opts"; Layout.preferredWidth: 140; font.pixelSize: Style.fontSizeS; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurfaceVariant }
    }

    Repeater {
        model: root.candidatePartitions
        delegate: RowLayout {
            id: row
            required property var modelData
            required property int index
            Layout.fillWidth: true
            spacing: Style.marginS

            ColumnLayout {
                Layout.preferredWidth: 130
                spacing: 0
                Text { text: row.modelData.name; font.pixelSize: Style.fontSizeS; color: Color.mOnSurface; elide: Text.ElideMiddle; Layout.fillWidth: true }
                Text { text: root.formatSize(row.modelData.sizeBytes) + (row.modelData.fstype ? " · " + row.modelData.fstype : ""); font.pixelSize: Style.fontSizeXS; color: Color.mOnSurfaceVariant }
            }

            NTextInput {
                Layout.fillWidth: true
                placeholderText: "/ or /home etc."
                text: root.assignments[row.index] ? root.assignments[row.index].mountpoint : ""
                onTextChanged: {
                    if (!root.assignments[row.index]) return;
                    var copy = root.assignments.slice();
                    copy[row.index] = Object.assign({}, copy[row.index], { mountpoint: text });
                    root.assignments = copy;
                }
            }

            NComboBox {
                Layout.preferredWidth: 110
                model: [
                    { key: "ext4",  name: "ext4" },
                    { key: "btrfs", name: "btrfs" },
                    { key: "xfs",   name: "xfs" },
                    { key: "vfat",  name: "vfat" },
                ]
                currentKey: root.assignments[row.index] ? root.assignments[row.index].fstype : "ext4"
                onSelected: function(key) {
                    var copy = root.assignments.slice();
                    copy[row.index] = Object.assign({}, copy[row.index], { fstype: key });
                    root.assignments = copy;
                }
            }

            NCheckbox {
                Layout.preferredWidth: 70
                Layout.alignment: Qt.AlignHCenter
                label: ""
                checked: root.assignments[row.index] ? root.assignments[row.index].formatRequested : false
                onToggled: function(val) {
                    var copy = root.assignments.slice();
                    copy[row.index] = Object.assign({}, copy[row.index], { formatRequested: val });
                    root.assignments = copy;
                }
            }

            NTextInput {
                Layout.preferredWidth: 140
                placeholderText: "(optional)"
                text: root.assignments[row.index] ? root.assignments[row.index].mountOpts : ""
                onTextChanged: {
                    if (!root.assignments[row.index]) return;
                    var copy = root.assignments.slice();
                    copy[row.index] = Object.assign({}, copy[row.index], { mountOpts: text });
                    root.assignments = copy;
                }
            }
        }
    }
}
