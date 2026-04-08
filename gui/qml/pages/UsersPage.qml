import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller
import "../components/Format.js" as Fmt

Item {
    id: root

    property string hostname: hostnameField.text
    property string username: usernameField.text
    property string userPassword: userPwField.text
    property string userPasswordConfirm: userPwConfirmField.text
    property string rootPassword: rootPwField.text
    property string rootPasswordConfirm: rootPwConfirmField.text
    property string selectedShell: ""
    property bool autologin: false

    // Consumed by WizardNavBar to gate Next while on this page.
    readonly property bool valid:
        InstallerBackend.forbiddenLogins.indexOf(root.username) === -1
        && InstallerBackend.forbiddenHostnames.indexOf(root.hostname) === -1
        && root.userPassword === root.userPasswordConfirm
        && root.rootPassword === root.rootPasswordConfirm

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

        PageTitle {
            Layout.fillWidth: true
            title: "User Configuration"
        }

        NTextInput {
            id: hostnameField
            Layout.fillWidth: true
            label: "Hostname"
            placeholderText: "cachyos"
        }

        InlineError {
            text: (root.hostname.length > 0 && InstallerBackend.forbiddenHostnames.indexOf(root.hostname) !== -1)
                  ? "'" + root.hostname + "' is reserved and cannot be used as a hostname." : ""
        }

        NTextInput {
            id: usernameField
            Layout.fillWidth: true
            label: "Username"
            placeholderText: "user"
        }

        InlineError {
            text: (root.username.length > 0 && InstallerBackend.forbiddenLogins.indexOf(root.username) !== -1)
                  ? "'" + root.username + "' is a reserved system account and cannot be used." : ""
        }

        PasswordField {
            id: userPwField
            Layout.fillWidth: true
            label: "User Password"
        }

        PasswordStrengthMeter {
            Layout.fillWidth: true
            password: root.userPassword
        }

        PasswordField {
            id: userPwConfirmField
            Layout.fillWidth: true
            label: "Confirm Password"
        }

        InlineError {
            text: (root.userPasswordConfirm.length > 0 && root.userPassword !== root.userPasswordConfirm)
                  ? "Passwords do not match." : ""
        }

        PasswordField {
            id: rootPwField
            Layout.fillWidth: true
            label: "Root Password"
        }

        PasswordField {
            id: rootPwConfirmField
            Layout.fillWidth: true
            label: "Confirm Root Password"
        }

        InlineError {
            text: (root.rootPasswordConfirm.length > 0 && root.rootPassword !== root.rootPasswordConfirm)
                  ? "Root passwords do not match." : ""
        }

        NComboBox {
            Layout.fillWidth: true
            label: "Default Shell"
            model: Fmt.toComboModel(InstallerBackend.shellList)
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
