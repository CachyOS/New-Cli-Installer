import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import CachyInstaller

Item {
    id: root
    property string selectedKeymap: ""

    property var filteredKeymaps: {
        var filter = keymapFilter.text.toLowerCase();
        if (filter.length === 0) return InstallerBackend.keymapList;
        var result = [];
        for (var i = 0; i < InstallerBackend.keymapList.length; ++i) {
            if (InstallerBackend.keymapList[i].toLowerCase().indexOf(filter) >= 0)
                result.push(InstallerBackend.keymapList[i]);
        }
        return result;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginXL
        spacing: Style.marginL

        Text {
            text: "Keyboard Layout"
            font.pixelSize: Style.fontSizeXXL
            font.weight: Style.fontWeightBold
            color: Color.mOnSurface
        }

        TextField {
            id: keymapFilter
            Layout.fillWidth: true
            placeholderText: "Search keyboard layout..."
            color: Color.mOnSurface
            background: Rectangle {
                color: Color.mSurfaceVariant
                radius: Style.iRadiusS
                border.color: keymapFilter.activeFocus ? Color.mPrimary : Color.mOutline
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.filteredKeymaps
            delegate: ItemDelegate {
                width: ListView.view.width
                text: modelData
                highlighted: modelData === root.selectedKeymap
                onClicked: root.selectedKeymap = modelData
                contentItem: Text {
                    text: parent.text
                    font.pixelSize: Style.fontSizeM
                    color: parent.highlighted ? Color.mOnPrimary : Color.mOnSurface
                }
                background: Rectangle {
                    color: parent.highlighted ? Color.mPrimary : "transparent"
                    radius: Style.radiusXS
                }
            }
        }

        // Test area
        TextField {
            Layout.fillWidth: true
            placeholderText: "Type here to test your keyboard layout..."
            color: Color.mOnSurface
            font.pixelSize: Style.fontSizeL
            background: Rectangle {
                color: Color.mSurfaceVariant
                radius: Style.iRadiusS
                border.color: Color.mOutline
            }
        }
    }
}
