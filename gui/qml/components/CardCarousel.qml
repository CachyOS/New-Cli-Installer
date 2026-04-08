import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets

// Reusable horizontal "card carousel": a large centered card showing an image,
// title and description, flanked by prev/next arrows, with clickable dot
// indicators below. Shared by the Desktop and Bootloader pages so the two stay
// visually identical.
//
// Each model entry is expected to expose an image, a title and a description;
// the field names are configurable via the *Role properties so callers can keep
// their existing data shape (e.g. the desktop list uses "name" for the title).
Item {
    id: root

    property var model: []
    property string titleRole: "title"
    property string descriptionRole: "description"
    property string imageRole: "image"
    property url fallbackImage: "qrc:/qt/qml/CachyInstaller/assets/desktops/no-selection.png"
    property int descriptionMaxLines: 2

    // Optional control rendered at the trailing edge of the dot bar (e.g. an
    // "Advanced" button). Declared inline by the caller; resolves ids from the
    // caller's lexical scope.
    property Component trailingComponent: null

    property alias currentIndex: carousel.currentIndex
    readonly property int count: carousel.count

    function decrementCurrentIndex() {
        if (carousel.currentIndex > 0)
            carousel.decrementCurrentIndex();
    }
    function incrementCurrentIndex() {
        if (carousel.currentIndex < carousel.count - 1)
            carousel.incrementCurrentIndex();
    }

    // Left arrow button
    Rectangle {
        id: leftBtn
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: 40; height: 80
        radius: Style.radiusS
        color: leftMA.containsMouse ? Color.mHover : Color.mSurfaceVariant
        opacity: carousel.currentIndex > 0 ? 1.0 : 0.3
        z: 2

        Text {
            anchors.centerIn: parent
            text: "◀"
            font.pixelSize: Style.fontSizeXL
            color: leftMA.containsMouse ? Color.mOnHover : Color.mOnSurface
        }
        MouseArea {
            id: leftMA
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.decrementCurrentIndex()
        }
    }

    // Right arrow button
    Rectangle {
        id: rightBtn
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 40; height: 80
        radius: Style.radiusS
        color: rightMA.containsMouse ? Color.mHover : Color.mSurfaceVariant
        opacity: carousel.currentIndex < carousel.count - 1 ? 1.0 : 0.3
        z: 2

        Text {
            anchors.centerIn: parent
            text: "▶"
            font.pixelSize: Style.fontSizeXL
            color: rightMA.containsMouse ? Color.mOnHover : Color.mOnSurface
        }
        MouseArea {
            id: rightMA
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.incrementCurrentIndex()
        }
    }

    // Main carousel
    ListView {
        id: carousel
        anchors.top: parent.top
        anchors.bottom: bottomBar.top
        anchors.bottomMargin: Style.marginS
        anchors.left: leftBtn.right
        anchors.leftMargin: Style.marginS
        anchors.right: rightBtn.left
        anchors.rightMargin: Style.marginS

        readonly property real cardW: width * 0.75

        orientation: ListView.Horizontal
        snapMode: ListView.SnapOneItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: (width - cardW) / 2
        preferredHighlightEnd: preferredHighlightBegin + cardW
        highlightMoveDuration: 300
        clip: true
        model: root.model
        interactive: true

        // Leading/trailing spacers the width of the centered highlight offset, so
        // the first and last cards can sit centered without the view needing to
        // overscroll.
        header: Item { width: carousel.preferredHighlightBegin; height: carousel.height }
        footer: Item { width: carousel.preferredHighlightBegin; height: carousel.height }

        delegate: Item {
            id: cardDelegate
            width: carousel.cardW
            height: carousel.height

            required property var modelData
            required property int index

            readonly property bool isCurrent: ListView.isCurrentItem

            transform: Scale {
                origin.x: cardDelegate.width / 2
                origin.y: cardDelegate.height / 2
                xScale: cardDelegate.isCurrent ? 1.0 : 0.88
                yScale: cardDelegate.isCurrent ? 1.0 : 0.88
                Behavior on xScale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                Behavior on yScale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            }
            opacity: isCurrent ? 1.0 : 0.45
            Behavior on opacity { NumberAnimation { duration: 200 } }

            Rectangle {
                anchors.fill: parent
                anchors.margins: Style.marginXS
                radius: Style.radiusM
                color: Color.mSurfaceVariant
                border.color: cardDelegate.isCurrent ? Color.mPrimary : Color.mOutline
                border.width: cardDelegate.isCurrent ? 2 : 1

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        AnimatedImage {
                            anchors.fill: parent
                            anchors.margins: Style.marginXS
                            source: cardDelegate.modelData[root.imageRole] || root.fallbackImage
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            smooth: true

                            onStatusChanged: {
                                if (status === AnimatedImage.Error)
                                    source = root.fallbackImage;
                            }
                        }
                    }

                    // Name + description bar at bottom
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: nameCol.implicitHeight + Style.marginM * 2
                        color: Qt.alpha(Color.mSurface, 0.85)
                        radius: Style.radiusM
                        // Only round bottom corners
                        Rectangle {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: parent.radius
                            color: parent.color
                        }

                        ColumnLayout {
                            id: nameCol
                            anchors.fill: parent
                            anchors.margins: Style.marginM
                            spacing: 4

                            Text {
                                Layout.fillWidth: true
                                text: cardDelegate.modelData[root.titleRole] || ""
                                font.pixelSize: Style.fontSizeL
                                font.weight: Style.fontWeightBold
                                color: Color.mPrimary
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                Layout.fillWidth: true
                                text: cardDelegate.modelData[root.descriptionRole] || ""
                                font.pixelSize: Style.fontSizeXS
                                color: Color.mOnSurfaceVariant
                                wrapMode: Text.Wrap
                                elide: Text.ElideRight
                                maximumLineCount: root.descriptionMaxLines
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }
        }
    }

    // Bottom bar: dot indicators (+ optional trailing control)
    RowLayout {
        id: bottomBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 32

        Item { Layout.fillWidth: true }

        Row {
            spacing: 5
            Repeater {
                model: carousel.count
                Rectangle {
                    required property int index
                    width: index === carousel.currentIndex ? 14 : 8
                    height: 8
                    radius: 4
                    color: index === carousel.currentIndex ? Color.mPrimary : Color.mOutline
                    Behavior on width { NumberAnimation { duration: 150 } }
                    Behavior on color { ColorAnimation { duration: 150 } }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        onClicked: carousel.currentIndex = parent.index
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        Loader {
            active: root.trailingComponent !== null
            sourceComponent: root.trailingComponent
        }
    }
}
