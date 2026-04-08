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
            text: "Welcome to CachyOS"
            font.pixelSize: Style.fontSizeXXXL
            font.weight: Style.fontWeightBold
            color: Color.mOnSurface
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "This installer will guide you through the installation process."
            font.pixelSize: Style.fontSizeL
            color: Color.mOnSurfaceVariant
            horizontalAlignment: Text.AlignHCenter
        }

        // System checks
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.marginM

            Row {
                spacing: Style.marginM
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: InstallerBackend.isRoot ? Color.mPrimary : Color.mError
                }
                Text {
                    text: InstallerBackend.isRoot ? "Running as root" : "Not running as root (required)"
                    font.pixelSize: Style.fontSizeM
                    color: Color.mOnSurface
                }
            }

            Row {
                spacing: Style.marginM
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: InstallerBackend.isConnected ? Color.mPrimary : Color.mError
                }
                Text {
                    text: InstallerBackend.isConnected ? "Network connected" : "No network connection"
                    font.pixelSize: Style.fontSizeM
                    color: Color.mOnSurface
                }
            }

            Row {
                spacing: Style.marginM
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: Color.mPrimary
                }
                Text {
                    text: InstallerBackend.isUefi ? "UEFI mode" : "BIOS mode"
                    font.pixelSize: Style.fontSizeM
                    color: Color.mOnSurface
                }
            }
        }
    }
}
