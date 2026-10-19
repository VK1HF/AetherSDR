#pragma once

#include <QRectF>

class QWidget;

namespace AetherSDR::mac {

void updateNativeTitleVisibility(QWidget* window);
QRectF nativeCaptionBounds(const QWidget* window);

// The unified title bar's empty NSToolbar takes every right-click over it and
// shows its own "Icon and Text / Icon Only" menu, so a Qt control drawn there
// never sees one. This hands a right-click over `area` (the title bar) to the
// nearest widget under the pointer that asks for a custom context menu, and
// lets every other click through untouched.
void relayRightClicks(QWidget* area);

}
