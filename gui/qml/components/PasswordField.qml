import QtQuick
import noctalia.Widgets

// NTextInput preconfigured for password entry: masked input, no clear button.
// Read the entered value via the inherited `text` property.
NTextInput {
    showClearButton: false
    Component.onCompleted: inputItem.echoMode = TextInput.Password
}
