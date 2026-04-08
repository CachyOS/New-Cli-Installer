import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import CachyInstaller

ApplicationWindow {
    id: window
    visible: true
    width: 960
    height: 680
    minimumWidth: 800
    minimumHeight: 600
    title: "CachyOS Installer"
    color: Color.mSurface

    Component.onCompleted: {
        InstallerBackend.initialize();
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Step indicator sidebar
        StepIndicator {
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            currentStep: InstallerBackend.currentPage
        }

        // Main content area
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Page content
            StackLayout {
                id: pageStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: InstallerBackend.currentPage

                WelcomePage { id: welcomePage }
                LocalePage { id: localePage }
                KeyboardPage { id: keyboardPage }
                BootloaderPage { id: bootloaderPage }
                PartitionPage {
                    id: partitionPage
                    // Bootloader is chosen first, so the partition page can correctly
                    // hide ZFS native encryption under grub.
                    selectedBootloader: bootloaderPage.selectedBootloader
                }
                DesktopPage { id: desktopPage }
                PackagesPage {
                    id: packagesPage
                    // Sync the chosen DE so its packages aren't offered again as
                    // "additional" (the desktop step already pulls them in).
                    selectedDesktop: desktopPage.selectedDesktop
                    serverMode: desktopPage.serverMode
                }
                UsersPage { id: usersPage }
                SummaryPage { id: summaryPage }
                InstallPage { id: installPage }
                FinishedPage { id: finishedPage }
            }

            // Navigation bar
            WizardNavBar {
                Layout.fillWidth: true
            }
        }
    }
}
