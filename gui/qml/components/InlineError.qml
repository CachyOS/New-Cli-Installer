import QtQuick
import QtQuick.Layouts
import noctalia.Commons

// Small red, wrapped validation message. Shown only when it has text, so callers
// just bind `text` to a conditional string (or override `visible`).
Text {
    Layout.fillWidth: true
    visible: text.length > 0
    font.pixelSize: Style.fontSizeS
    color: Color.mError
    wrapMode: Text.Wrap
}
