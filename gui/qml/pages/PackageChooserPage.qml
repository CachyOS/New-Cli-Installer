import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Item {
    id: root
    property string selectedKernel: ""

    function toComboModel(list) {
        var result = [];
        for (var i = 0; i < list.length; ++i)
            result.push({key: list[i], name: list[i]});
        return result;
    }

    Connections {
        target: InstallerBackend
        function onListsPopulated() {
            if (root.selectedKernel === "" && InstallerBackend.kernelList.length > 0)
                root.selectedKernel = InstallerBackend.kernelList[0];
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginXL
        spacing: Style.marginL

        Text {
            text: "Package Selection"
            font.pixelSize: Style.fontSizeXXL
            font.weight: Style.fontWeightBold
            color: Color.mOnSurface
        }

        NComboBox {
            Layout.fillWidth: true
            label: "Kernel"
            model: root.toComboModel(InstallerBackend.kernelList)
            currentKey: root.selectedKernel
            onSelected: function(key) { root.selectedKernel = key; }
        }

        Item { Layout.fillHeight: true }
    }
}
