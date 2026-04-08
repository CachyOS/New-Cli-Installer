import QtQuick
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets

Text {
  id: root

  property bool richTextEnabled: false
  property bool markdownTextEnabled: false
  property string family: ""
  property real pointSize: Style.fontSizeM
  property bool applyUiScale: true
  property real fontScale: {
    if (applyUiScale) {
      return 1.0 * Style.uiScaleRatio;
    }
    return 1.0;
  }
  property var features: ({})

  opacity: enabled ? 1.0 : 0.6
  font.family: root.family
  font.weight: Style.fontWeightMedium
  font.pointSize: Math.max(1, root.pointSize * fontScale)
  font.features: root.features
  color: Color.mOnSurface
  elide: Text.ElideRight
  wrapMode: Text.NoWrap
  verticalAlignment: Text.AlignVCenter

  textFormat: {
    if (root.richTextEnabled) {
      return Text.RichText;
    } else if (root.markdownTextEnabled) {
      return Text.MarkdownText;
    }
    return Text.PlainText;
  }
}
