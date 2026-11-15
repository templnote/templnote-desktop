import QtQuick

TextBlock {
  verticalPadding: 0
  fontPixelSize: Math.max(20, 34 - ((headingLevel - 1) * 4))
  fontBold: true
  showBullet: false
}
