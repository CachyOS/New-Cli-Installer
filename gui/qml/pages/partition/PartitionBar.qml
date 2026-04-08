import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import "../../components/Format.js" as Fmt
import "../../components/Partitions.js" as Pt

// Horizontal proportional bar rendering a device's partitions as size-weighted
// colored segments, with unallocated free space shown as the empty track between
// them. Clicking a segment selects it.
Item {
    id: root

    // Existing partitions: [{ number, startBytes, endBytes, sizeBytes, device, fstype, flags }].
    property var partitions: []
    // Unallocated regions: [{ startBytes, endBytes, sizeBytes }] — labelled gaps.
    property var freeRegions: []
    // Total addressable bytes used to scale every segment. When <= 0 it is derived
    // from the furthest partition/region end (a disk with no reported size).
    property real totalBytes: 0
    // Currently selected partition number, or -1 when nothing is selected.
    property int selectedNumber: -1

    // Emitted with the partition number when a segment is clicked.
    signal partitionClicked(int number)

    implicitHeight: 52

    readonly property real _total: {
        if (root.totalBytes > 0)
            return root.totalBytes;
        var max = 1;
        for (var i = 0; i < root.partitions.length; ++i)
            max = Math.max(max, root.partitions[i].endBytes);
        for (var j = 0; j < root.freeRegions.length; ++j)
            max = Math.max(max, root.freeRegions[j].endBytes);
        return max;
    }

    // Stable colour per filesystem family (shared with the editors), falling back
    // to the theme primary for unknown types.
    function _fsColor(fstype) {
        return Pt.fsColor(fstype) || Color.mPrimary;
    }

    Rectangle {
        id: track
        anchors.fill: parent
        radius: Style.radiusS
        color: Qt.alpha(Color.mOnSurfaceVariant, 0.12)
        border.color: Color.mOutline
        border.width: Style.borderS
        clip: true

        // Free-space gaps are just the empty track with a centred label.
        Repeater {
            model: root.freeRegions
            delegate: Item {
                required property var modelData
                x: track.width * (modelData.startBytes / root._total)
                width: Math.max(1, track.width * (modelData.sizeBytes / root._total))
                height: track.height
                Text {
                    anchors.centerIn: parent
                    visible: parent.width > 64
                    text: "free\n" + Fmt.formatSize(modelData.sizeBytes)
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Style.fontSizeXS
                    color: Color.mOnSurfaceVariant
                }
            }
        }

        Repeater {
            model: root.partitions
            delegate: Rectangle {
                id: seg
                required property var modelData
                readonly property bool sel: modelData.number === root.selectedNumber
                x: track.width * (modelData.startBytes / root._total)
                width: Math.max(2, track.width * (modelData.sizeBytes / root._total))
                height: track.height
                color: root._fsColor(modelData.fstype)
                opacity: seg.sel ? 1.0 : 0.82
                border.color: seg.sel ? Color.mOnSurface : Qt.darker(color, 1.3)
                border.width: seg.sel ? Style.borderL : Style.borderS

                // Used-space fill: a darker band along the bottom
                // sized to the filesystem's used bytes, when known.
                Rectangle {
                    anchors { left: parent.left; bottom: parent.bottom; margins: parent.border.width }
                    visible: (seg.modelData.usedBytes || 0) > 0 && seg.modelData.sizeBytes > 0
                    width: Math.max(0, (seg.width - 2 * parent.border.width)
                           * ((seg.modelData.usedBytes || 0) / seg.modelData.sizeBytes))
                    height: Math.round(parent.height * 0.3)
                    color: Qt.darker(seg.color, 1.6)
                }

                Column {
                    anchors.centerIn: parent
                    visible: seg.width > 58
                    spacing: 0
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: seg.width - Style.marginS
                        text: seg.modelData.device
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        font.pixelSize: Style.fontSizeXS
                        font.weight: Style.fontWeightSemiBold
                        color: "white"
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (seg.modelData.fstype ? seg.modelData.fstype
                              : (seg.modelData.partType ? seg.modelData.partType : "—"))
                              + "  " + Fmt.formatSize(seg.modelData.sizeBytes)
                        font.pixelSize: Style.fontSizeXS
                        color: "white"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.partitionClicked(seg.modelData.number)
                }
            }
        }
    }
}
