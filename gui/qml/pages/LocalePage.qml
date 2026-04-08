import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import CachyInstaller

Item {
    id: root

    property string selectedLocale: ""
    property string selectedTimezone: ""

    property var filteredLocales: {
        var filter = localeFilter.text.toLowerCase();
        if (filter.length === 0) return InstallerBackend.localeList;
        var result = [];
        for (var i = 0; i < InstallerBackend.localeList.length; ++i) {
            if (InstallerBackend.localeList[i].toLowerCase().indexOf(filter) >= 0)
                result.push(InstallerBackend.localeList[i]);
        }
        return result;
    }

    property var filteredTimezones: {
        var filter = tzFilter.text.toLowerCase();
        if (filter.length === 0) return InstallerBackend.timezoneList;
        var result = [];
        for (var i = 0; i < InstallerBackend.timezoneList.length; ++i) {
            if (InstallerBackend.timezoneList[i].toLowerCase().indexOf(filter) >= 0)
                result.push(InstallerBackend.timezoneList[i]);
        }
        return result;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginXL
        spacing: Style.marginL

        Text {
            text: "Locale & Timezone"
            font.pixelSize: Style.fontSizeXXL
            font.weight: Style.fontWeightBold
            color: Color.mOnSurface
        }

        // Locale
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.marginS

            Text {
                text: "Locale"
                font.pixelSize: Style.fontSizeL
                font.weight: Style.fontWeightSemiBold
                color: Color.mOnSurface
            }

            TextField {
                id: localeFilter
                Layout.fillWidth: true
                placeholderText: "Search locale..."
                color: Color.mOnSurface
                background: Rectangle {
                    color: Color.mSurfaceVariant
                    radius: Style.iRadiusS
                    border.color: localeFilter.activeFocus ? Color.mPrimary : Color.mOutline
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                clip: true
                model: root.filteredLocales
                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData
                    highlighted: modelData === root.selectedLocale
                    onClicked: root.selectedLocale = modelData
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
        }

        // Timezone
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Style.marginS

            Text {
                text: "Timezone"
                font.pixelSize: Style.fontSizeL
                font.weight: Style.fontWeightSemiBold
                color: Color.mOnSurface
            }

            TextField {
                id: tzFilter
                Layout.fillWidth: true
                placeholderText: "Search timezone..."
                color: Color.mOnSurface
                background: Rectangle {
                    color: Color.mSurfaceVariant
                    radius: Style.iRadiusS
                    border.color: tzFilter.activeFocus ? Color.mPrimary : Color.mOutline
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.filteredTimezones
                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData
                    highlighted: modelData === root.selectedTimezone
                    onClicked: root.selectedTimezone = modelData
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
        }
    }
}
