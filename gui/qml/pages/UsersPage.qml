import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Item {
    id: root

    property string hostname: ""
    property string username: ""
    property string userPassword: ""
    property string userPasswordConfirm: ""
    property string rootPassword: ""
    property string rootPasswordConfirm: ""
    property string selectedShell: ""
    property bool autologin: false

    // Consumed by WizardNavBar to gate Next while on this page.
    readonly property bool valid:
        InstallerBackend.forbiddenLogins.indexOf(root.username) === -1
        && InstallerBackend.forbiddenHostnames.indexOf(root.hostname) === -1

    function toComboModel(list) {
        var result = [];
        for (var i = 0; i < list.length; ++i)
            result.push({key: list[i], name: list[i]});
        return result;
    }

    Connections {
        target: InstallerBackend
        function onListsPopulated() {
            if (root.selectedShell === "" && InstallerBackend.shellList.length > 0)
                root.selectedShell = InstallerBackend.shellList[0];
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginXL
        spacing: Style.marginS

        Text {
            text: "User Configuration"
            font.pixelSize: Style.fontSizeXXL
            font.weight: Style.fontWeightBold
            color: Color.mOnSurface
        }

        NTextInput {
            Layout.fillWidth: true
            label: "Hostname"
            placeholderText: "cachyos"
            onEditingFinished: root.hostname = text
            Component.onCompleted: root.hostname = Qt.binding(function() { return text; })
        }

        Text {
            Layout.fillWidth: true
            visible: root.hostname.length > 0 && InstallerBackend.forbiddenHostnames.indexOf(root.hostname) !== -1
            text: "'" + root.hostname + "' is reserved and cannot be used as a hostname."
            font.pixelSize: Style.fontSizeS
            color: Color.mError
        }

        NTextInput {
            Layout.fillWidth: true
            label: "Username"
            placeholderText: "user"
            onEditingFinished: root.username = text
            Component.onCompleted: root.username = Qt.binding(function() { return text; })
        }

        Text {
            Layout.fillWidth: true
            visible: root.username.length > 0 && InstallerBackend.forbiddenLogins.indexOf(root.username) !== -1
            text: "'" + root.username + "' is a reserved system account and cannot be used."
            font.pixelSize: Style.fontSizeS
            color: Color.mError
        }

        NTextInput {
            Layout.fillWidth: true
            label: "User Password"
            showClearButton: false
            Component.onCompleted: {
                inputItem.echoMode = TextInput.Password;
                root.userPassword = Qt.binding(function() { return text; });
            }
        }

        NTextInput {
            Layout.fillWidth: true
            label: "Confirm Password"
            showClearButton: false
            Component.onCompleted: {
                inputItem.echoMode = TextInput.Password;
                root.userPasswordConfirm = Qt.binding(function() { return text; });
            }
        }

        NTextInput {
            Layout.fillWidth: true
            label: "Root Password"
            showClearButton: false
            Component.onCompleted: {
                inputItem.echoMode = TextInput.Password;
                root.rootPassword = Qt.binding(function() { return text; });
            }
        }

        NTextInput {
            Layout.fillWidth: true
            label: "Confirm Root Password"
            showClearButton: false
            Component.onCompleted: {
                inputItem.echoMode = TextInput.Password;
                root.rootPasswordConfirm = Qt.binding(function() { return text; });
            }
        }

        NComboBox {
            Layout.fillWidth: true
            label: "Default Shell"
            model: root.toComboModel(InstallerBackend.shellList)
            currentKey: root.selectedShell
            onSelected: function(key) { root.selectedShell = key; }
        }

        NCheckbox {
            Layout.fillWidth: true
            label: "Log in automatically (no password prompt at boot)"
            checked: root.autologin
            onToggled: function(val) { root.autologin = val; }
        }

        Item { Layout.fillHeight: true }
    }
}
