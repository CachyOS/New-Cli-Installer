import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets

ColumnLayout {
    id: root

    // Full block-device list (inventory.blockDevices). The picker filters this
    // down to partitions whose `parent` matches @ref parentDevice.
    property var blockDevices: []
    // Disk the user picked above (e.g. "/dev/sda"). Partitions with this as
    // their parent are the candidates.
    property string parentDevice: ""
    // Bubble-up of the currently selected partition's device path.
    property string selectedPartition: ""

    spacing: Style.marginS

    readonly property var candidates: {
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

    function describe(d) {
        var parts = [root.formatSize(d.sizeBytes)];
        if (d.fstype && d.fstype !== "")
            parts.push(d.fstype);
        if (d.label && d.label !== "")
            parts.push("label: " + d.label);
        return parts.join(", ");
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: candidates.length === 0
              ? "No existing partitions found on " + (parentDevice || "the selected disk") + ". Pick a different disk or use Erase mode."
              : "Pick one partition to format and use as root. Other partitions on the disk are left untouched."
        font.pixelSize: Style.fontSizeM
        color: Color.mOnSurfaceVariant
    }

    ButtonGroup {
        id: group
        onCheckedButtonChanged: {
            if (group.checkedButton && group.checkedButton.devicePath)
                root.selectedPartition = group.checkedButton.devicePath;
        }
    }

    Repeater {
        model: root.candidates
        delegate: NRadioButton {
            required property var modelData
            property string devicePath: modelData.name
            Layout.fillWidth: true
            text: modelData.name + "  —  " + root.describe(modelData)
            checked: root.selectedPartition === devicePath
            ButtonGroup.group: group
        }
    }
}
