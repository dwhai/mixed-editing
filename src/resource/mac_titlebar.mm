//
// macOS-specific title bar integration helpers.
//

#include "../widget/include/mac_titlebar.h"

#import <AppKit/AppKit.h>

namespace Mixed {
    namespace {
        API_AVAILABLE(macos(11.0))
        NSButton *makeSymbolButton(NSString *symbolName, NSString *tip) {
            NSImageSymbolConfiguration *config =
                    [NSImageSymbolConfiguration configurationWithPointSize:15
                                                                    weight:NSFontWeightRegular];
            NSImage *image = [[NSImage imageWithSystemSymbolName:symbolName
                                       accessibilityDescription:tip]
                    imageWithSymbolConfiguration:config];

            NSButton *button = [NSButton buttonWithImage:image target:nil action:nil];
            button.bordered = NO;
            button.imagePosition = NSImageOnly;
            button.toolTip = tip;
            button.contentTintColor = [NSColor secondaryLabelColor];
            button.translatesAutoresizingMaskIntoConstraints = NO;
            [button.widthAnchor constraintEqualToConstant:28].active = YES;
            return button;
        }
    }

    void configureMacTitleBar(void *viewPtr) {
        if (viewPtr == nullptr) {
            return;
        }
        NSView *view = (__bridge NSView *) viewPtr;
        NSWindow *window = [view window];
        if (window == nil) {
            return;
        }

        window.titlebarAppearsTransparent = YES;
        window.titleVisibility = NSWindowTitleHidden;

        // --- Leading accessory: brand label, sits right after the traffic lights ---
        NSTextField *brand = [NSTextField labelWithString:@"剪映专业版"];
        brand.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
        brand.textColor = [NSColor labelColor];
        brand.translatesAutoresizingMaskIntoConstraints = NO;

        NSView *leadingContainer = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 120, 28)];
        [leadingContainer addSubview:brand];
        [NSLayoutConstraint activateConstraints:@[
            [brand.leadingAnchor constraintEqualToAnchor:leadingContainer.leadingAnchor constant:6],
            [brand.trailingAnchor constraintEqualToAnchor:leadingContainer.trailingAnchor constant:-6],
            [brand.centerYAnchor constraintEqualToAnchor:leadingContainer.centerYAnchor]
        ]];

        NSTitlebarAccessoryViewController *leadingVC = [[NSTitlebarAccessoryViewController alloc] init];
        leadingVC.view = leadingContainer;
        leadingVC.layoutAttribute = NSLayoutAttributeLeading;

        // --- Trailing accessory: action icons on the far right of the title bar ---
        NSView *trailingContainer = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 110, 28)];

        if (@available(macOS 11.0, *)) {
            NSButton *help = makeSymbolButton(@"questionmark.circle", @"帮助");
            NSButton *message = makeSymbolButton(@"message", @"消息");
            NSButton *settings = makeSymbolButton(@"gearshape", @"设置");

            NSStackView *stack = [NSStackView stackViewWithViews:@[help, message, settings]];
            stack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
            stack.spacing = 4;
            stack.translatesAutoresizingMaskIntoConstraints = NO;
            [trailingContainer addSubview:stack];
            [NSLayoutConstraint activateConstraints:@[
                [stack.trailingAnchor constraintEqualToAnchor:trailingContainer.trailingAnchor constant:-10],
                [stack.centerYAnchor constraintEqualToAnchor:trailingContainer.centerYAnchor]
            ]];
        }

        NSTitlebarAccessoryViewController *trailingVC = [[NSTitlebarAccessoryViewController alloc] init];
        trailingVC.view = trailingContainer;
        trailingVC.layoutAttribute = NSLayoutAttributeTrailing;

        [window addTitlebarAccessoryViewController:leadingVC];
        [window addTitlebarAccessoryViewController:trailingVC];
    }
}
