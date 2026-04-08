import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import CachyInstaller

Item {
    id: root

    property var logLines: []

    Connections {
        target: InstallerBackend
        function onLogLine(line) {
            root.logLines = root.logLines.concat([line]);
            logView.positionViewAtEnd();
        }
        function onInstallFinished(success, error) {
            InstallerBackend.currentPage = 9;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginXL
        spacing: Style.marginL

        Text {
            text: "Installing CachyOS"
            font.pixelSize: Style.fontSizeXXL
            font.weight: Style.fontWeightBold
            color: Color.mOnSurface
        }

        // Progress bar
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.marginS

            Text {
                text: InstallerBackend.installStatus
                font.pixelSize: Style.fontSizeM
                color: Color.mOnSurface
            }

            ProgressBar {
                Layout.fillWidth: true
                value: InstallerBackend.installProgress
                background: Rectangle {
                    implicitHeight: 8
                    radius: 4
                    color: Color.mSurfaceVariant
                }
                contentItem: Item {
                    implicitHeight: 8
                    Rectangle {
                        width: parent.width * InstallerBackend.installProgress
                        height: parent.height
                        radius: 4
                        color: Color.mPrimary
                    }
                }
            }

            Text {
                text: Math.round(InstallerBackend.installProgress * 100) + "%"
                font.pixelSize: Style.fontSizeS
                color: Color.mOnSurfaceVariant
            }
        }

        // Log output
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Style.radiusS
            color: Color.mSurfaceVariant

            ListView {
                id: logView
                anchors.fill: parent
                anchors.margins: Style.marginM
                clip: true
                model: root.logLines
                delegate: Text {
                    width: logView.width
                    text: modelData
                    font.pixelSize: Style.fontSizeXS
                    font.family: "monospace"
                    color: Color.mOnSurfaceVariant
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
