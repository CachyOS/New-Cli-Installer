pragma Singleton

import QtQuick

QtObject {
  id: root

  // Key Colors: Noctalia-default dark palette
  readonly property color mPrimary: "#fff59b"
  readonly property color mOnPrimary: "#0e0e43"
  readonly property color mSecondary: "#a9aefe"
  readonly property color mOnSecondary: "#0e0e43"
  readonly property color mTertiary: "#9BFECE"
  readonly property color mOnTertiary: "#0e0e43"

  // Utility Colors
  readonly property color mError: "#FD4663"
  readonly property color mOnError: "#0e0e43"

  // Surface and Variant Colors
  readonly property color mSurface: "#070722"
  readonly property color mOnSurface: "#f3edf7"

  readonly property color mSurfaceVariant: "#11112d"
  readonly property color mOnSurfaceVariant: "#7c80b4"

  readonly property color mOutline: "#21215F"
  readonly property color mShadow: "#070722"

  readonly property color mHover: "#9BFECE"
  readonly property color mOnHover: "#0e0e43"

  function resolveColorKey(key) {
    switch (key) {
    case "primary":
      return root.mPrimary;
    case "secondary":
      return root.mSecondary;
    case "tertiary":
      return root.mTertiary;
    case "error":
      return root.mError;
    default:
      return root.mOnSurface;
    }
  }

  // Translucency not used in installer. just return the color as-is
  readonly property bool isTransitioning: false

  function smartAlpha(baseColor, minAlpha) {
    return baseColor;
  }

  function resolveOnColorKey(key) {
    switch (key) {
    case "primary":
      return root.mOnPrimary;
    case "secondary":
      return root.mOnSecondary;
    case "tertiary":
      return root.mOnTertiary;
    case "error":
      return root.mOnError;
    default:
      return root.mSurface;
    }
  }
}
