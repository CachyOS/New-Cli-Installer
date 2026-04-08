import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Item {
    id: root

    property string selectedBootloader: ""

    // Map the firmware-filtered bootloader metadata (key/label/description, see
    // cachyos::installer::data::bootloader_options) into the shape CardCarousel
    // expects, deriving the card image from the bootloader key.
    function toCarouselModel(options) {
        var result = [];
        for (var i = 0; i < options.length; ++i) {
            var opt = options[i];
            result.push({
                key:         opt.key,
                title:       opt.label,
                description: opt.description,
                image:       "qrc:/qt/qml/CachyInstaller/assets/bootloaders/" + opt.key + ".jxl",
            });
        }
        return result;
    }

    Connections {
        target: InstallerBackend
        function onListsPopulated() {
            if (root.selectedBootloader === "" && InstallerBackend.bootloaderOptions.length > 0)
                root.selectedBootloader = InstallerBackend.bootloaderOptions[0].key;
        }
    }

    // Keyboard navigation
    focus: true
    Keys.onLeftPressed: carousel.decrementCurrentIndex()
    Keys.onRightPressed: carousel.incrementCurrentIndex()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginM
        spacing: Style.marginS

        PageTitle {
            Layout.fillWidth: true
            title: "Bootloader"
            titleSize: Style.fontSizeXL
        }

        CardCarousel {
            id: carousel
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.toCarouselModel(InstallerBackend.bootloaderOptions)
            descriptionMaxLines: 4
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    root.selectedBootloader = model[currentIndex].key;
            }
        }
    }
}
