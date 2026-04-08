import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Item {
    id: root

    // Defaults preserve current behaviour: carry the live ISO's network
    // configuration over, do not install every chwd hardware profile.
    property bool carryLiveNetwork: true
    property bool installChwdProfiles: false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginXL
        spacing: Style.marginL

        Text {
            text: "Installation Summary"
            font.pixelSize: Style.fontSizeXXL
            font.weight: Style.fontWeightBold
            color: Color.mOnSurface
        }

        Text {
            text: "Review your selections before proceeding. Click 'Install' to begin."
            font.pixelSize: Style.fontSizeM
            color: Color.mOnSurfaceVariant
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Style.radiusS
            color: Color.mSurfaceVariant

            GridLayout {
                anchors.fill: parent
                anchors.margins: Style.marginL
                columns: 2
                columnSpacing: Style.marginL
                rowSpacing: Style.marginM

                Text { text: "Locale"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: localePage.selectedLocale || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Timezone"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: localePage.selectedTimezone || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Keyboard"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: keyboardPage.selectedKeymap || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Device"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: partitionPage.selectedDevice || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Filesystem"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: partitionPage.selectedFs || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Desktop"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: desktopPage.serverMode ? "Server (none)" : (desktopPage.selectedDesktop || "Not set"); font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Kernel"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: packagePage.selectedKernel || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Bootloader"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: bootloaderPage.selectedBootloader || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Hostname"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: usersPage.hostname || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Username"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: usersPage.username || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Shell"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: usersPage.selectedShell || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Item { Layout.fillHeight: true; Layout.columnSpan: 2 }
            }
        }

        NCheckbox {
            Layout.fillWidth: true
            label: "Carry live network configuration"
            description: "Copy the live ISO's NetworkManager system-connections into the installed system."
            checked: root.carryLiveNetwork
            onToggled: function(c) { root.carryLiveNetwork = c; }
        }

        NCheckbox {
            Layout.fillWidth: true
            label: "Install all matching chwd hardware profiles"
            description: "Run chwd against the target to install every applicable hardware-driver profile."
            checked: root.installChwdProfiles
            onToggled: function(c) { root.installChwdProfiles = c; }
        }
    }
}
