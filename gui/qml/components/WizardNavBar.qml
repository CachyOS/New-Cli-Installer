import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Rectangle {
    id: root
    height: 64
    color: Color.mSurfaceVariant

    RowLayout {
        anchors.fill: parent
        anchors.margins: Style.marginL

        // Cancel button
        NButton {
            text: "Cancel"
            backgroundColor: Color.mError
            textColor: Color.mOnError
            visible: !InstallerBackend.isInstalling && InstallerBackend.currentPage < 9
            onClicked: Qt.quit()
        }

        Item { Layout.fillWidth: true }

        // Back button
        NButton {
            text: "Back"
            outlined: true
            visible: InstallerBackend.canGoBack
            onClicked: InstallerBackend.currentPage = InstallerBackend.currentPage - 1
        }

        // Next / Install button
        NButton {
            // Per-page input validity gates that canGoNext can't see (it lives
            // in C++ and the inputs are on the QML pages).
            readonly property bool pageValid:
                InstallerBackend.currentPage !== 7 || usersPage.valid

            text: InstallerBackend.currentPage === 8 ? "Install" : "Next"
            visible: InstallerBackend.canGoNext && pageValid
            onClicked: {
                if (InstallerBackend.currentPage === 8) {
                    var selections = {
                        "device":              partitionPage.selectedDevice,
                        "filesystem":          partitionPage.selectedFs,
                        "kernel":              packagePage.selectedKernel,
                        "desktop":             desktopPage.selectedDesktop,
                        "bootloader":          bootloaderPage.selectedBootloader,
                        "serverMode":          desktopPage.serverMode,
                        "locale":              localePage.selectedLocale,
                        "timezone":            localePage.selectedTimezone,
                        "keymap":              keyboardPage.selectedKeymap,
                        "hostname":            usersPage.hostname,
                        "username":            usersPage.username,
                        "userPassword":        usersPage.userPassword,
                        "rootPassword":        usersPage.rootPassword,
                        "shell":               usersPage.selectedShell,
                        "autologin":           usersPage.autologin,
                        "carryLiveNetwork":    summaryPage.carryLiveNetwork,
                        "installChwdProfiles": summaryPage.installChwdProfiles
                    };
                    InstallerBackend.startInstallation(selections);
                    InstallerBackend.currentPage = 8;
                } else {
                    InstallerBackend.currentPage = InstallerBackend.currentPage + 1;
                }
            }
        }

        // Close button (only on finished page)
        NButton {
            text: "Close"
            visible: InstallerBackend.currentPage === 10
            onClicked: Qt.quit()
        }
    }
}
