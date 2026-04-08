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

        NTextInput {
            Layout.fillWidth: true
            label: "Username"
            placeholderText: "user"
            onEditingFinished: root.username = text
            Component.onCompleted: root.username = Qt.binding(function() { return text; })
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

        Item { Layout.fillHeight: true }
    }
}
