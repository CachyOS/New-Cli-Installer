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

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.marginL
            visible: InstallerBackend.errorMessage.length === 0

            NButton {
                text: "Reboot Now"
                onClicked: {
                    // Falls through to a message if the reboot can't be dispatched
                    // (e.g. not running as root); the user stays in the live env.
                    if (!InstallerBackend.reboot())
                        root.rebootFailed = true;
                }
            }

            NButton {
                text: "Stay in Live Environment"
                outlined: true
                onClicked: Qt.quit()
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            visible: root.rebootFailed
            text: "Could not trigger a reboot automatically. Please reboot manually."
            font.pixelSize: Style.fontSizeM
            color: Color.mError
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }

    property bool rebootFailed: false
}
