import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets

ColumnLayout {
    id: root

    // One of: "Erase" | "Replace" | "Alongside" | "Manual".
    property string mode: "Erase"

    readonly property var modes: [
        { key: "Erase",     label: "Erase disk",
          desc: "Wipe the target disk and let the installer partition it (recommended)." },
        { key: "Replace",   label: "Replace a partition",
          desc: "Reuse an existing partition for the root filesystem; leave other partitions alone." },
        { key: "Alongside", label: "Install alongside",
          desc: "Shrink an existing OS partition and install in the freed space." },
        { key: "Manual",    label: "Manual",
          desc: "Build the mountpoint table yourself from existing partitions." },
    ]

    spacing: Style.marginS

    ButtonGroup {
        id: group
        onCheckedButtonChanged: {
            if (group.checkedButton && group.checkedButton.modeKey)
                root.mode = group.checkedButton.modeKey;
        }
    }

    Repeater {
        model: root.modes
        delegate: ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 0

            NRadioButton {
                property string modeKey: modelData.key
                text: modelData.label
                checked: root.mode === modelData.key
                ButtonGroup.group: group
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Style.marginXL
                Layout.bottomMargin: Style.marginS
                wrapMode: Text.Wrap
                text: modelData.desc
                font.pixelSize: Style.fontSizeS
                color: Color.mOnSurfaceVariant
            }
        }
    }
}
