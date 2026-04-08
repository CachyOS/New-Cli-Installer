import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller
import "../components/Netinstall.js" as NI

Item {
    id: root

    // Synced from DesktopPage so the chosen DE's packages aren't double-offered
    // (the desktop step already installs them). Kept for the sync contract.
    property string selectedDesktop: "kde"
    property bool serverMode: false

    // Raw recursive group tree from the backend, plus the live selection/expand
    // state. Reassigned wholesale on every change so bindings re-evaluate.
    property var groups: []
    property var selection: ({})
    property var expanded: ({})
    property bool advancedMode: false
    property bool loading: false

    // Flattened package list handed to the installer (see WizardNavBar).
    readonly property var additionalPackages: NI.collectPackages(groups, selection)
    readonly property var bundles: NI.bundleRows(groups)
    readonly property var rows: NI.flatten(groups, selection, expanded, searchField.text)

    Component.onCompleted: root.reload()

    function reload() {
        root.loading = true;
        InstallerBackend.fetchNetinstallGroups();
    }

    function toggleGroup(key, value, cascade) {
        root.selection = NI.setSelected(root.groups, root.selection, key, value, cascade);
    }

    function toggleExpand(key) {
        var e = {};
        for (var k in root.expanded)
            e[k] = root.expanded[k];
        e[key] = !(e[key] === true);
        root.expanded = e;
    }

    Connections {
        target: InstallerBackend
        function onNetinstallGroupsReady(groups) {
            root.groups = groups;
            root.selection = NI.defaultSelection(groups);
            root.expanded = ({});
            root.loading = false;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginM
        spacing: Style.marginS

        // Title + view toggle
        RowLayout {
            Layout.fillWidth: true

            PageTitle {
                Layout.fillWidth: true
                title: "Optional Software"
                titleSize: Style.fontSizeXL
            }

            NButton {
                text: root.advancedMode ? "Simple view" : "Advanced…"
                outlined: true
                onClicked: root.advancedMode = !root.advancedMode
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.advancedMode
                ? "Browse every optional package group. Locked groups are required and cannot be unchecked."
                : "Pick extra software bundles to install now. Not sure? Just continue — nothing extra is selected by default."
            font.pixelSize: Style.fontSizeM
            color: Color.mOnSurfaceVariant
            wrapMode: Text.Wrap
        }

        Text {
            Layout.fillWidth: true
            visible: root.loading
            text: "Loading package groups…"
            font.pixelSize: Style.fontSizeM
            color: Color.mOnSurfaceVariant
        }

        // ── Simple bundle grid ────────────────────────────────────────────────
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.advancedMode && !root.loading
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Flow {
                width: root.width - 2 * Style.marginM
                spacing: Style.marginM

                Repeater {
                    model: root.bundles

                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        readonly property bool on: NI.groupFullySelected(root.groups, root.selection, modelData.key)

                        width: 280
                        height: 116
                        radius: Style.radiusM
                        color: on ? Qt.alpha(Color.mPrimary, 0.15) : Color.mSurfaceVariant
                        border.color: on ? Color.mPrimary : Color.mOutline
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Style.marginM
                            spacing: Style.marginXS

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Style.marginS

                                NIcon {
                                    icon: card.modelData.group.icon !== "" ? card.modelData.group.icon : "package"
                                    pointSize: Style.fontSizeXL
                                    color: card.on ? Color.mPrimary : Color.mOnSurface
                                }

                                NText {
                                    Layout.fillWidth: true
                                    text: card.modelData.group.name
                                    pointSize: Style.fontSizeL
                                    font.weight: Style.fontWeightSemiBold
                                    color: Color.mOnSurface
                                    elide: Text.ElideRight
                                }

                                NIcon {
                                    visible: card.on
                                    icon: "check"
                                    pointSize: Style.fontSizeL
                                    color: Color.mPrimary
                                }
                            }

                            NText {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                text: card.modelData.group.description
                                pointSize: Style.fontSizeS
                                color: Color.mOnSurfaceVariant
                                wrapMode: Text.Wrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleGroup(card.modelData.key, !card.on, true)
                        }
                    }
                }
            }
        }

        // ── Advanced searchable tree ──────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.advancedMode && !root.loading
            spacing: Style.marginS

            NTextInput {
                id: searchField
                Layout.fillWidth: true
                inputIconName: "search"
                placeholderText: "Search groups and packages…"
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Style.radiusS
                color: Color.mSurfaceVariant
                clip: true

                ListView {
                    id: treeList
                    anchors.fill: parent
                    anchors.margins: Style.marginS
                    model: root.rows
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Item {
                        id: row
                        required property var modelData
                        width: treeList.width - (treeList.ScrollBar.vertical.width || 0)
                        height: 32

                        RowLayout {
                            anchors.fill: parent
                            spacing: Style.marginS

                            Item {
                                Layout.preferredWidth: row.modelData.depth * 18
                            }

                            // Expand/collapse affordance for groups & subgroups.
                            NIcon {
                                visible: row.modelData.kind !== "package"
                                icon: row.modelData.expanded ? "chevron-down" : "chevron-right"
                                pointSize: Style.fontSizeM
                                color: Color.mOnSurfaceVariant

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.toggleExpand(row.modelData.key)
                                }
                            }

                            // Package leaf: read-only, follows its group.
                            NText {
                                visible: row.modelData.kind === "package"
                                Layout.fillWidth: true
                                text: "• " + row.modelData.name
                                pointSize: Style.fontSizeS
                                color: Color.mOnSurfaceVariant
                                elide: Text.ElideRight
                            }

                            // Group / subgroup: checkable (locked when critical).
                            NCheckbox {
                                visible: row.modelData.kind !== "package"
                                Layout.fillWidth: true
                                enabled: !row.modelData.critical
                                label: row.modelData.critical
                                    ? row.modelData.name + "  (required)"
                                    : row.modelData.name
                                description: row.modelData.description
                                labelSize: Style.fontSizeM
                                checked: row.modelData.checked || row.modelData.critical
                                onToggled: function(val) {
                                    root.toggleGroup(row.modelData.key, val, false);
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: treeList.count === 0
                        text: "No matching packages."
                        font.pixelSize: Style.fontSizeM
                        color: Color.mOnSurfaceVariant
                    }
                }
            }
        }
    }
}
