import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller
import "../../components/Format.js" as Fmt
import "../../components/Partitions.js" as Pt

// shrink an existing partition with a
// draggable splitter and install into the space that frees up. The user picks a
// partition, drags the handle to choose how much to keep, and the editor emits a
// resize op (shrink the existing fs+partition) plus a create op for the freed
// gap. PartitionPage applies the ops and formats the new partition as root.
ColumnLayout {
    id: root

    // Disk the user picked above (e.g. "/dev/sda").
    property string parentDevice: ""
    // Full block-device inventory, used to read the target disk's total size.
    property var blockDevices: []

    // Pending operation list ({ kind, … } maps) — empty until a valid split is set.
    property var ops: []

    spacing: Style.marginM

    // A usable root install needs at least this much freed space.
    readonly property real minInstallBytes: 5 * 1024 * 1024 * 1024

    // Total size of the target disk (for the proportional bar scale).
    readonly property real diskTotalBytes: {
        for (var i = 0; i < root.blockDevices.length; ++i) {
            if (root.blockDevices[i].name === root.parentDevice)
                return root.blockDevices[i].sizeBytes;
        }
        return 0;
    }

    // Existing partitions + free gaps, re-queried whenever the disk changes.
    readonly property var partitions: parentDevice === ""
        ? [] : InstallerBackend.partitionsForDevice(parentDevice)
    readonly property var freeRegions: parentDevice === ""
        ? [] : InstallerBackend.freeSpaceRegions(parentDevice)

    // Only filesystems gucc can shrink may be split (shared with the manual editor).
    function _canShrink(fstype) {
        return Pt.canShrink(fstype);
    }
    readonly property var shrinkable: {
        var out = [];
        for (var i = 0; i < root.partitions.length; ++i) {
            if (root._canShrink(root.partitions[i].fstype))
                out.push(root.partitions[i]);
        }
        return out;
    }

    // Selection + chosen split state.
    property int selectedNumber: -1
    property var selectedPart: null
    // {min,max} bytes for the *kept* portion of the selected partition.
    property var bounds: ({})
    // How many bytes to keep in the existing partition (the rest is freed).
    property real keepBytes: 0
    property string boundsError: ""

    // Largest the kept portion may be while still leaving room for a root install.
    readonly property real maxKeepBytes: root.selectedPart
        ? Math.max(0, root.selectedPart.sizeBytes - root.minInstallBytes) : 0
    readonly property real minKeepBytes: root.bounds.min ? root.bounds.min : 0
    readonly property real freedBytes: root.selectedPart
        ? root.selectedPart.sizeBytes - root.keepBytes : 0
    readonly property bool splitValid: root.selectedPart !== null
        && root.boundsError === ""
        && root.keepBytes >= root.minKeepBytes
        && root.keepBytes <= root.maxKeepBytes
        && root.freedBytes >= root.minInstallBytes

    function selectPartition(number) {
        var part = null;
        for (var i = 0; i < root.partitions.length; ++i) {
            if (root.partitions[i].number === number) { part = root.partitions[i]; break; }
        }
        root.selectedNumber = number;
        root.selectedPart = part;
        root.bounds = ({});
        root.boundsError = "";
        if (!part)
            return;
        var b = InstallerBackend.shrinkableBounds(parentDevice, part.number, part.fstype, part.sizeBytes);
        if (!b || b.min === undefined) {
            root.boundsError = "Could not determine how far " + part.device + " can shrink: "
                             + InstallerBackend.plannerError;
            return;
        }
        root.bounds = b;
        // Start the split halfway between the minimum and the largest usable keep.
        var hi = Math.max(b.min, root.selectedPart.sizeBytes - root.minInstallBytes);
        root.keepBytes = (b.min + hi) / 2;
    }

    // Re-derive the op list whenever the split changes.
    function _rebuildOps() {
        if (!root.splitValid) { root.ops = []; return; }
        var p = root.selectedPart;
        var keepEnd = p.startBytes + Math.round(root.keepBytes) - 1;  // inclusive end
        root.ops = [
            {
                kind:       "resize",
                number:     p.number,
                fstype:     p.fstype,
                sizeBytes:  Math.round(root.keepBytes),
                endBytes:   keepEnd,
            },
            {
                kind:       "create",
                startBytes: keepEnd + 1,
                endBytes:   p.endBytes,
                fstype:     "",
            },
        ];
    }
    onKeepBytesChanged: _rebuildOps()
    onSplitValidChanged: _rebuildOps()

    // Reset everything when the disk changes.
    onParentDeviceChanged: { root.selectedNumber = -1; root.selectedPart = null; root.ops = []; }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: {
            if (root.partitions.length === 0 && !InstallerBackend.isRoot)
                return "The installer must run as root to read the partition table of "
                       + (root.parentDevice || "the selected disk")
                       + ". Re-run with root privileges to shrink a partition.";
            if (root.shrinkable.length === 0)
                return "No shrinkable partition was found on " + (root.parentDevice || "the selected disk")
                       + ". Only ext4/ext3/ext2, NTFS and Btrfs filesystems can be resized.";
            return "Pick a partition to shrink, then drag the handle to choose how much space to free for the new install. Your data in the kept portion is preserved.";
        }
        font.pixelSize: Style.fontSizeM
        color: Color.mOnSurfaceVariant
    }

    // Whole-disk overview; click a segment to select it for shrinking.
    PartitionBar {
        Layout.fillWidth: true
        visible: root.partitions.length > 0
        partitions: root.partitions
        freeRegions: root.freeRegions
        totalBytes: root.diskTotalBytes
        selectedNumber: root.selectedNumber
        onPartitionClicked: function(number) {
            // Ignore clicks on filesystems we cannot shrink.
            if (root._canShrink(_fstypeFor(number)))
                root.selectPartition(number);
        }
    }

    function _fstypeFor(number) {
        for (var i = 0; i < root.partitions.length; ++i) {
            if (root.partitions[i].number === number) return root.partitions[i].fstype;
        }
        return "";
    }

    // Quick-pick radio list (keyboard-accessible alternative to clicking the bar).
    ButtonGroup { id: shrinkGroup }
    Flow {
        Layout.fillWidth: true
        visible: root.shrinkable.length > 0
        spacing: Style.marginM
        Repeater {
            model: root.shrinkable
            delegate: NRadioButton {
                required property var modelData
                text: modelData.device + " (" + (modelData.fstype || "—")
                      + ", " + Fmt.formatSize(modelData.sizeBytes) + ")"
                checked: root.selectedNumber === modelData.number
                ButtonGroup.group: shrinkGroup
                onClicked: root.selectPartition(modelData.number)
            }
        }
    }

    InlineError {
        text: root.boundsError
    }

    // The split editor: a full-width bar for the selected partition divided into
    // [ keep | new install ] with a draggable handle.
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.selectedPart !== null && root.boundsError === ""
        spacing: Style.marginXS

        Item {
            id: split
            Layout.fillWidth: true
            implicitHeight: 56

            readonly property real keepFrac: root.selectedPart
                ? root.keepBytes / root.selectedPart.sizeBytes : 0.5
            // Drag bounds expressed as fractions of the partition width.
            readonly property real minFrac: root.selectedPart
                ? root.minKeepBytes / root.selectedPart.sizeBytes : 0
            readonly property real maxFrac: root.selectedPart
                ? root.maxKeepBytes / root.selectedPart.sizeBytes : 1

            Rectangle {
                id: keepRect
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: parent.width * split.keepFrac
                radius: Style.radiusS
                color: Pt.fsColor(root.selectedPart ? root.selectedPart.fstype : "") || "#4F86C6"
                Column {
                    anchors.centerIn: parent
                    visible: keepRect.width > 70
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Keep"; color: "white"
                        font.pixelSize: Style.fontSizeXS; font.weight: Style.fontWeightSemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Fmt.formatSize(root.keepBytes); color: "white"; font.pixelSize: Style.fontSizeXS
                    }
                }
            }
            Rectangle {
                anchors { left: keepRect.right; right: parent.right; top: parent.top; bottom: parent.bottom }
                radius: Style.radiusS
                color: Color.mPrimary
                Column {
                    anchors.centerIn: parent
                    visible: parent.width > 70
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "New install"; color: Color.mOnPrimary
                        font.pixelSize: Style.fontSizeXS; font.weight: Style.fontWeightSemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Fmt.formatSize(root.freedBytes); color: Color.mOnPrimary; font.pixelSize: Style.fontSizeXS
                    }
                }
            }

            // Draggable handle.
            Rectangle {
                id: handle
                width: 10
                anchors { top: parent.top; bottom: parent.bottom }
                x: Math.round(keepRect.width - width / 2)
                radius: Style.radiusXS
                color: Color.mOnSurface
                border.color: Color.mSurface
                border.width: Style.borderS

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -Style.marginS
                    cursorShape: Qt.SizeHorCursor
                    onPositionChanged: function(mouse) {
                        var px = handle.x + handle.width / 2 + mouse.x - handle.width / 2;
                        var frac = px / split.width;
                        frac = Math.max(split.minFrac, Math.min(split.maxFrac, frac));
                        root.keepBytes = frac * root.selectedPart.sizeBytes;
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: {
                if (!root.selectedPart)
                    return "";
                if (root.maxKeepBytes < root.minKeepBytes)
                    return root.selectedPart.device + " is too full to free "
                           + Fmt.formatSize(root.minInstallBytes) + " for an install.";
                return "Keep " + Fmt.formatSize(root.keepBytes) + " on " + root.selectedPart.device
                       + ", free " + Fmt.formatSize(root.freedBytes) + " for the new install"
                       + " (minimum keep " + Fmt.formatSize(root.minKeepBytes) + ").";
            }
            font.pixelSize: Style.fontSizeS
            color: root.splitValid ? Color.mOnSurfaceVariant : Color.mError
        }
    }
}
