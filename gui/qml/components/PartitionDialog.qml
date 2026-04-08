import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets

// Shared modal chrome for the manual-partition editor's dialogs: a titled card
// with a body (the caller's form fields, declared as children), an inline error
// line and a Cancel / confirm button row. The confirm button emits `confirmed()`
// without closing, so the caller validates and calls close() itself.
Popup {
    id: dlg

    property string title: ""
    property string confirmText: qsTr("OK")
    property color confirmColor: Color.mPrimary
    property color confirmTextColor: Color.mOnPrimary
    // Validation message shown above the buttons; cleared on each open.
    property alias errorText: errorLabel.text
    // Form fields go here (default children).
    default property alias content: body.data

    signal confirmed()

    anchors.centerIn: Overlay.overlay
    implicitWidth: 460
    modal: true
    focus: true
    padding: Style.marginL

    background: Rectangle {
        color: Color.mSurface
        radius: Style.radiusM
        border.color: Color.mOutline
        border.width: Style.borderS
    }

    onOpened: errorLabel.text = ""

    contentItem: ColumnLayout {
        spacing: Style.marginM

        Text {
            visible: dlg.title !== ""
            text: dlg.title
            font.pixelSize: Style.fontSizeL
            font.weight: Style.fontWeightBold
            color: Color.mOnSurface
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: Style.marginM
        }

        InlineError { id: errorLabel }

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.marginS
            Item { Layout.fillWidth: true }
            NButton { text: qsTr("Cancel"); outlined: true; onClicked: dlg.close() }
            NButton {
                text: dlg.confirmText
                backgroundColor: dlg.confirmColor
                textColor: dlg.confirmTextColor
                onClicked: dlg.confirmed()
            }
        }
    }
}
