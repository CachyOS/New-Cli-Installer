import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Item {
    id: root

    // Defaults preserve current behaviour: carry the live ISO's network
    // configuration over. chwd hardware-profile installation is mandatory on
    // CachyOS, so there's no toggle — the backend always runs it.
    property bool carryLiveNetwork: true

    // Human-readable description of what will happen to the disk. Recomputed
    // whenever this page is shown (stagedPartitionSummary has no NOTIFY).
    property var partitioningLines: []

    function refreshPartitioning() {
        var staged = InstallerBackend.stagedPartitionSummary();
        if (staged.length > 0) {
            root.partitioningLines = staged;
            return;
        }
        var mode = partitionPage.partitionMode;
        if (mode === "Erase" && partitionPage.selectedFs === "zfs")
            root.partitioningLines = ["Erase " + (partitionPage.selectedDevice || "disk")
                + " — ZFS pool \"" + (partitionPage.zfsPoolName || "zpcachyos") + "\""
                + (partitionPage.zfsEncrypt ? " (encrypted)" : "")];
        else if (mode === "Erase" && partitionPage.useDefaultLayout)
            root.partitioningLines = ["Erase " + (partitionPage.selectedDevice || "disk")
                + " — automatic layout" + (partitionPage.selectedFs ? " (" + partitionPage.selectedFs + ")" : "")];
        else if (mode === "Replace")
            root.partitioningLines = ["Format " + (partitionPage.selectedReplacePartition || "selected partition")
                + " as " + (partitionPage.selectedFs || "?") + ", mount at /"];
        else if (mode === "Manual")
            root.partitioningLines = ["Mount selected existing partitions"];
        else if (mode === "Erase")
            root.partitioningLines = ["Custom layout on " + (partitionPage.selectedDevice || "disk")];
        else
            root.partitioningLines = [mode];
    }

    onVisibleChanged: if (visible) refreshPartitioning()
    Component.onCompleted: refreshPartitioning()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginXL
        spacing: Style.marginL

        PageTitle {
            Layout.fillWidth: true
            title: "Installation Summary"
            subtitle: "Review your selections before proceeding. Click 'Install' to begin."
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

                Text { text: "Partitioning"; Layout.alignment: Qt.AlignTop; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Repeater {
                        model: root.partitioningLines
                        delegate: Text {
                            required property string modelData
                            Layout.fillWidth: true
                            text: "• " + modelData
                            font.pixelSize: Style.fontSizeM
                            color: Color.mOnSurface
                            wrapMode: Text.Wrap
                        }
                    }
                    Text {
                        visible: root.partitioningLines.length === 0
                        text: "Not set"
                        font.pixelSize: Style.fontSizeM
                        color: Color.mOnSurface
                    }
                }

                Text { text: "Desktop"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: desktopPage.serverMode ? "Server (none)" : (desktopPage.selectedDesktop || "Not set"); font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Kernel"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text { text: desktopPage.selectedKernel || "Not set"; font.pixelSize: Style.fontSizeM; color: Color.mOnSurface }

                Text { text: "Optional packages"; font.pixelSize: Style.fontSizeM; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurface }
                Text {
                    Layout.fillWidth: true
                    text: packagesPage.additionalPackages.length === 0
                        ? "None"
                        : packagesPage.additionalPackages.length + " package(s): " + packagesPage.additionalPackages.join(", ")
                    font.pixelSize: Style.fontSizeM
                    color: Color.mOnSurface
                    wrapMode: Text.Wrap
                }

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

        // Pre-flight validation error from startInstallation(), shown when the
        // backend rejected the selections so the user can fix the missing field.
        InlineError {
            visible: !InstallerBackend.isInstalling && InstallerBackend.errorMessage.length > 0
            text: InstallerBackend.errorMessage
            font.weight: Style.fontWeightSemiBold
        }
    }
}
