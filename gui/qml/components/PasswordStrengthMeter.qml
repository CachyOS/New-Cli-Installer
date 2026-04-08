import QtQuick
import QtQuick.Layouts
import noctalia.Commons

// Lightweight password-strength meter: a four-segment bar plus a label, driven
// by a heuristic score (length + character-class diversity). Hidden information
// while the password is empty. Reusable for any password field
// by binding the `password` property.
Item {
    id: root

    property string password: ""

    // 0..4 strength score.
    readonly property int score: {
        const p = root.password;
        if (p.length === 0)
            return 0;
        let s = 0;
        if (p.length >= 8) s += 1;
        if (p.length >= 12) s += 1;
        let classes = 0;
        if (/[a-z]/.test(p)) classes += 1;
        if (/[A-Z]/.test(p)) classes += 1;
        if (/[0-9]/.test(p)) classes += 1;
        if (/[^A-Za-z0-9]/.test(p)) classes += 1;
        if (classes >= 3) s += 1;
        if (classes >= 4) s += 1;
        // Anything shorter than 6 chars can never rate above "Too weak".
        if (p.length < 6) return Math.min(s, 0);
        return Math.min(s, 4);
    }

    readonly property var _levels: [
        { label: "Too weak", color: Color.mError },
        { label: "Weak",     color: Color.mError },
        { label: "Fair",     color: Color.mPrimary },
        { label: "Good",     color: Color.mSecondary },
        { label: "Strong",   color: Color.mTertiary }
    ]
    readonly property color barColor: _levels[score].color
    readonly property string label: _levels[score].label

    implicitHeight: column.implicitHeight
    visible: root.password.length > 0

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Style.marginXS

        // Four segments; filled up to the current score.
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.marginXS
            Repeater {
                model: 4
                Rectangle {
                    required property int index
                    Layout.fillWidth: true
                    height: 6
                    radius: 3
                    color: (root.score > index) ? root.barColor : Color.mSurfaceVariant
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
            }
        }

        Text {
            text: "Password strength: " + root.label
            font.pixelSize: Style.fontSizeXS
            color: root.barColor
        }
    }
}
