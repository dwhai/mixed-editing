//
// macOS-specific title bar integration helpers.
//

#ifndef MIXEDEDITING_MAC_TITLEBAR_H
#define MIXEDEDITING_MAC_TITLEBAR_H

namespace Mixed {
    // Merges the unified toolbar into the native title bar row (traffic-light row)
    // so the toolbar items sit on the same line as the window title.
    // viewPtr must be the NSView* obtained from QWidget::winId().
    void configureMacTitleBar(void *viewPtr);
}

#endif //MIXEDEDITING_MAC_TITLEBAR_H
