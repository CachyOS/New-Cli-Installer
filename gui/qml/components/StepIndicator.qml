import QtQuick
import QtQuick.Layouts
import noctalia.Commons

Rectangle {
    id: root

    property int currentStep: 0

    readonly property var steps: [
        "Welcome",
        "Locale",
        "Keyboard",
        "Partition",
        "Desktop",
        "Packages",
        "Users",
        "Summary",
        "Install",
        "Done"
    ]

    color: Color.mSurfaceVariant

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginL
        spacing: Style.marginXS

        // Logo area
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            Layout.bottomMargin: Style.marginL

            Text {
                anchors.centerIn: parent
                text: "CachyOS"
                font.pixelSize: Style.fontSizeXXL
                font.weight: Style.fontWeightBold
                color: Color.mPrimary
            }
        }

        // Step items
        Repeater {
            model: root.steps

            Rectangle {
                Layout.fillWidth: true
                height: 36
                radius: Style.radiusXS
                color: index === root.currentStep
                    ? Qt.alpha(Color.mPrimary, 0.15)
                    : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Style.marginM
                    anchors.rightMargin: Style.marginM
                    spacing: Style.marginM

                    // Step number circle
                    Rectangle {
                        width: 24
                        height: 24
                        radius: 12
                        color: {
                            if (index < root.currentStep)
                                return Color.mPrimary;
                            if (index === root.currentStep)
                                return Color.mPrimary;
                            return Color.mOutline;
                        }

                        Text {
                            anchors.centerIn: parent
                            text: index < root.currentStep ? "\u2713" : (index + 1).toString()
                            font.pixelSize: Style.fontSizeXS
                            font.weight: Style.fontWeightSemiBold
                            color: {
                                if (index <= root.currentStep)
                                    return Color.mOnPrimary;
                                return Color.mOnSurfaceVariant;
                            }
                        }
                    }

                    // Step label
                    Text {
                        Layout.fillWidth: true
                        text: modelData
                        font.pixelSize: Style.fontSizeM
                        font.weight: index === root.currentStep
                            ? Style.fontWeightSemiBold
                            : Style.fontWeightRegular
                        color: {
                            if (index === root.currentStep)
                                return Color.mOnSurface;
                            if (index < root.currentStep)
                                return Color.mOnSurfaceVariant;
                            return Color.mOnSurfaceVariant;
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
