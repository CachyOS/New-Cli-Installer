import QtQuick
import QtQuick.Layouts
import noctalia.Commons

// Standard page heading: a bold title with an optional muted subtitle below it.
// Shared by the wizard content pages so headings stay consistent.
ColumnLayout {
    id: root

    property string title: ""
    property string subtitle: ""
    // Pages with a compact layout (carousels) pass a smaller size.
    property real titleSize: Style.fontSizeXXL

    spacing: Style.marginXS

    Text {
        Layout.fillWidth: true
        text: root.title
        font.pixelSize: root.titleSize
        font.weight: Style.fontWeightBold
        color: Color.mOnSurface
        wrapMode: Text.Wrap
    }

    Text {
        Layout.fillWidth: true
        visible: root.subtitle !== ""
        text: root.subtitle
        font.pixelSize: Style.fontSizeM
        color: Color.mOnSurfaceVariant
        wrapMode: Text.Wrap
    }
}
