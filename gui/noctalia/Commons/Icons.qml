pragma Singleton

import QtQuick

QtObject {
  id: root

  readonly property string fontFamily: fontLoader.name
  readonly property string defaultIcon: IconsTabler.defaultIcon
  readonly property var icons: IconsTabler.icons
  readonly property var aliases: IconsTabler.aliases

  property FontLoader fontLoader: FontLoader {
    source: Qt.resolvedUrl("Assets/Fonts/tabler/noctalia-tabler-icons.ttf")
  }

  function get(iconName) {
    if (aliases[iconName] !== undefined) {
      iconName = aliases[iconName];
    }
    return icons[iconName];
  }
}
