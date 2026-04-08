pragma Singleton

import QtQuick

QtObject {
  id: root

  // Font size
  readonly property real fontSizeXXS: 8
  readonly property real fontSizeXS: 9
  readonly property real fontSizeS: 10
  readonly property real fontSizeM: 11
  readonly property real fontSizeL: 13
  readonly property real fontSizeXL: 16
  readonly property real fontSizeXXL: 18
  readonly property real fontSizeXXXL: 24

  // Font weight
  readonly property int fontWeightRegular: 400
  readonly property int fontWeightMedium: 500
  readonly property int fontWeightSemiBold: 600
  readonly property int fontWeightBold: 700

  // Scale ratios (hardcoded defaults)
  readonly property real uiScaleRatio: 1.0
  readonly property real radiusRatio: 1.0
  readonly property real iRadiusRatio: 1.0

  // Container radius: major layout sections (sidebars, cards, content panels)
  readonly property int radiusXXXS: Math.round(3 * radiusRatio)
  readonly property int radiusXXS: Math.round(4 * radiusRatio)
  readonly property int radiusXS: Math.round(8 * radiusRatio)
  readonly property int radiusS: Math.round(12 * radiusRatio)
  readonly property int radiusM: Math.round(16 * radiusRatio)
  readonly property int radiusL: Math.round(20 * radiusRatio)

  // Input radius: interactive elements (buttons, toggles, text fields)
  readonly property int iRadiusXXXS: Math.round(3 * iRadiusRatio)
  readonly property int iRadiusXXS: Math.round(4 * iRadiusRatio)
  readonly property int iRadiusXS: Math.round(8 * iRadiusRatio)
  readonly property int iRadiusS: Math.round(12 * iRadiusRatio)
  readonly property int iRadiusM: Math.round(16 * iRadiusRatio)
  readonly property int iRadiusL: Math.round(20 * iRadiusRatio)

  // Border
  readonly property int borderS: Math.max(1, Math.round(1 * uiScaleRatio))
  readonly property int borderM: Math.max(1, Math.round(2 * uiScaleRatio))
  readonly property int borderL: Math.max(1, Math.round(3 * uiScaleRatio))

  // Margins (for margins and spacing)
  readonly property int marginXXXS: Math.round(1 * uiScaleRatio)
  readonly property int marginXXS: Math.round(2 * uiScaleRatio)
  readonly property int marginXS: Math.round(4 * uiScaleRatio)
  readonly property int marginS: Math.round(6 * uiScaleRatio)
  readonly property int marginM: Math.round(9 * uiScaleRatio)
  readonly property int marginL: Math.round(13 * uiScaleRatio)
  readonly property int marginXL: Math.round(18 * uiScaleRatio)

  // Double margins
  readonly property int margin2XXXS: marginXXXS * 2
  readonly property int margin2XXS: marginXXS * 2
  readonly property int margin2XS: marginXS * 2
  readonly property int margin2S: marginS * 2
  readonly property int margin2M: marginM * 2
  readonly property int margin2L: marginL * 2
  readonly property int margin2XL: marginXL * 2

  // Opacity
  readonly property real opacityNone: 0.0
  readonly property real opacityLight: 0.25
  readonly property real opacityMedium: 0.5
  readonly property real opacityHeavy: 0.75
  readonly property real opacityAlmost: 0.95
  readonly property real opacityFull: 1.0

  // Shadows
  readonly property real shadowOpacity: 0.85
  readonly property real shadowBlur: 1.0
  readonly property int shadowBlurMax: 22
  readonly property real shadowHorizontalOffset: 0
  readonly property real shadowVerticalOffset: 2

  // Animation duration (ms)
  readonly property int animationFaster: 75
  readonly property int animationFast: 150
  readonly property int animationNormal: 300
  readonly property int animationSlow: 450
  readonly property int animationSlowest: 750

  // Delays
  readonly property int tooltipDelay: 300
  readonly property int tooltipDelayLong: 1200

  // Widgets base size
  readonly property real baseWidgetSize: 33
  readonly property real sliderWidth: 200

  function pixelAlignCenter(containerSize, contentSize) {
    return Math.round((containerSize - contentSize) / 2);
  }

  function toOdd(n) {
    return Math.floor(n / 2) * 2 + 1;
  }

  function toEven(n) {
    return Math.floor(n / 2) * 2;
  }
}
