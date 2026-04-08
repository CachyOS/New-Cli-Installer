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
                PartitionPage { id: partitionPage }
                DesktopPage { id: desktopPage }
                PackageChooserPage { id: packagePage }
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
