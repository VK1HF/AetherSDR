#include "NativeWindowTitle.h"

#include <QApplication>
#include <QCursor>
#include <QPointer>
#include <QWidget>
#include <QGuiApplication>
#import <AppKit/AppKit.h>

@interface AetherSDRTitleToolbar : NSToolbar
@property NSWindowToolbarStyle previousStyle;
@end

@implementation AetherSDRTitleToolbar
@end

namespace AetherSDR::mac {

static NSView* nativeView(const QWidget* window)
{
    if (!window || QGuiApplication::platformName() != QStringLiteral("cocoa")
        || !window->internalWinId()) {
        return nil;
    }
    return reinterpret_cast<NSView*>(window->internalWinId());
}

void updateNativeTitleVisibility(QWidget* window)
{
    NSWindow* native = nativeView(window).window;
    if (!native) {
        return;
    }
    const bool expanded = window->windowFlags().testFlag(Qt::ExpandedClientAreaHint);
    native.titleVisibility = expanded ? NSWindowTitleHidden : NSWindowTitleVisible;
    if (expanded && !native.toolbar) {
        auto* toolbar = [[AetherSDRTitleToolbar alloc] initWithIdentifier:@"AetherSDR.UnifiedTitleBar"];
        toolbar.previousStyle = native.toolbarStyle;
        toolbar.showsBaselineSeparator = NO;
        toolbar.allowsUserCustomization = NO;
        native.toolbarStyle = NSWindowToolbarStyleUnified;
        native.toolbar = toolbar;
        [toolbar release];
    } else if (!expanded && [native.toolbar isKindOfClass:[AetherSDRTitleToolbar class]]) {
        const NSWindowToolbarStyle previousStyle = ((AetherSDRTitleToolbar*)native.toolbar).previousStyle;
        native.toolbar = nil;
        native.toolbarStyle = previousStyle;
    }
}

void relayRightClicks(QWidget* area)
{
    QPointer<QWidget> guard(area);
    // A local monitor sees the event in -[NSApplication sendEvent:], before the
    // window routes it to the toolbar. It lives as long as the app; the guard
    // makes it inert once the title bar is gone.
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskRightMouseDown
                                          handler:^NSEvent*(NSEvent* event) {
        if (!guard || !guard->isVisible()) {
            return event;
        }
        const QPoint global = QCursor::pos();
        QWidget* hit = QApplication::widgetAt(global);
        if (!hit || (hit != guard && !guard->isAncestorOf(hit))) {
            return event;
        }
        for (QWidget* w = hit; w; w = w->parentWidget()) {
            if (w->contextMenuPolicy() == Qt::CustomContextMenu) {
                emit w->customContextMenuRequested(w->mapFromGlobal(global));
                return nil;   // handled; the toolbar menu does not open
            }
            if (w == guard) {
                break;
            }
        }
        return event;
    }];
}

QRectF nativeCaptionBounds(const QWidget* window)
{
    NSView* view = nativeView(window);
    NSWindow* native = view.window;
    if (!native || window->isFullScreen()
        || !window->windowFlags().testFlag(Qt::ExpandedClientAreaHint)) {
        return {};
    }
    QRectF bounds;
    for (NSWindowButton role : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
        NSButton* button = [native standardWindowButton:role];
        if (!button || button.hidden || !button.superview) {
            continue;
        }
        const NSRect rect = [button convertRect:button.bounds toView:view];
        const qreal top = view.flipped ? NSMinY(rect) : NSHeight(view.bounds) - NSMaxY(rect);
        bounds = bounds.united(QRectF(NSMinX(rect), top, NSWidth(rect), NSHeight(rect)));
    }
    return bounds;
}

}
