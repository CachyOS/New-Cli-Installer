import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Item {
    id: root
    property string selectedDevice: ""
    property string selectedFs: ""

    function toComboModel(list) {
        var result = [];
        for (var i = 0; i < list.length; ++i)
            result.push({key: list[i], name: list[i]});
        return result;
    }

    Connections {
        target: InstallerBackend
        function onListsPopulated() {
            if (root.selectedFs === "" && InstallerBackend.filesystemList.length > 0)
                root.selectedFs = InstallerBackend.filesystemList[0];
        }
        function onDeviceListChanged() {
            if (root.selectedDevice === "" && InstallerBackend.deviceList.length > 0)
                root.selectedDevice = InstallerBackend.deviceList[0];
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginXL
        spacing: Style.marginL

        Text {
            text: "Disk Partitioning"
            font.pixelSize: Style.fontSizeXXL
            font.weight: Style.fontWeightBold
            color: Color.mOnSurface
        }

        Text {
            text: "The selected disk will be erased and CachyOS will be installed on it."
            font.pixelSize: Style.fontSizeM
            color: Color.mOnSurfaceVariant
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        NComboBox {
            Layout.fillWidth: true
            label: "Target Device"
            model: root.toComboModel(InstallerBackend.deviceList)
            currentKey: root.selectedDevice
            onSelected: function(key) { root.selectedDevice = key; }
        }

        NComboBox {
            Layout.fillWidth: true
            label: "Filesystem"
            model: root.toComboModel(InstallerBackend.filesystemList)
            currentKey: root.selectedFs
            onSelected: function(key) { root.selectedFs = key; }
        }

        // Warning
        Rectangle {
            Layout.fillWidth: true
            height: warningText.implicitHeight + Style.margin2L
            radius: Style.radiusS
            color: Qt.alpha(Color.mError, 0.15)
            border.color: Color.mError
            border.width: Style.borderS

            Text {
                id: warningText
                anchors.centerIn: parent
                width: parent.width - Style.margin2L
                text: "Warning: All data on the selected device will be permanently erased!"
                font.pixelSize: Style.fontSizeM
                font.weight: Style.fontWeightSemiBold
                color: Color.mError
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Item { Layout.fillHeight: true }
    }
}
