import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Item {
    id: root

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Style.marginXL
        width: parent.width * 0.6

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: InstallerBackend.errorMessage.length > 0
                ? "Installation Failed"
                : "Installation Complete!"
            font.pixelSize: Style.fontSizeXXXL
            font.weight: Style.fontWeightBold
            color: InstallerBackend.errorMessage.length > 0
                ? Color.mError
                : Color.mPrimary
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            text: InstallerBackend.errorMessage.length > 0
                ? InstallerBackend.errorMessage
                : "CachyOS has been successfully installed on your system.\nYou can now reboot into your new installation."
            font.pixelSize: Style.fontSizeL
            color: Color.mOnSurfaceVariant
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        NButton {
            Layout.alignment: Qt.AlignHCenter
            text: "Reboot Now"
            visible: InstallerBackend.errorMessage.length === 0
            onClicked: {
                // TODO(vnepogodin): trigger reboot
                Qt.quit();
            }
        }
    }
}
