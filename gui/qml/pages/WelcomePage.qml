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

        // System checks — registry-driven (see installer-lib/include/cachyos/requirements.hpp)
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Style.marginM

            Repeater {
                model: InstallerBackend.requirements
                delegate: Row {
                    spacing: Style.marginM
                    required property var modelData
                    Rectangle {
                        width: 12; height: 12; radius: 6
                        color: modelData.satisfied
                            ? Color.mPrimary
                            : (modelData.mandatory ? Color.mError : Color.mTertiary)
                    }
                    Text {
                        text: modelData.satisfied ? modelData.messageOk : modelData.messageFailed
                        font.pixelSize: Style.fontSizeM
                        color: Color.mOnSurface
                    }
                }
            }

            NButton {
                Layout.alignment: Qt.AlignHCenter
                text: "Re-check"
                onClicked: InstallerBackend.refreshRequirements()
            }
        }
    }
}
