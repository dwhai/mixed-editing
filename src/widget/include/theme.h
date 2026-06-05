//
// Centralized theme: colors and the global Qt style sheet.
//
// Keeping every color and style rule in one place makes the look-and-feel easy
// to tweak and lets every widget stay free of inline styling.
//

#ifndef MIXEDEDITING_THEME_H
#define MIXEDEDITING_THEME_H

#include <QString>

namespace Mixed::Theme {
    // Core palette (light theme). Reuse these constants instead of hard-coding hex strings.
    namespace Color {
        inline constexpr auto Background = "#f4f5f7";
        inline constexpr auto Sidebar = "#ffffff";
        inline constexpr auto Card = "#ffffff";
        inline constexpr auto AccountCard = "#f4f5f7";
        inline constexpr auto DraftCard = "#ffffff";
        inline constexpr auto NavActive = "#e8eefc";
        inline constexpr auto NavHover = "#eef0f4";

        inline constexpr auto TextPrimary = "#1a1d24";
        inline constexpr auto TextSecondary = "#5a6172";
        inline constexpr auto TextMuted = "#8b92a3";
        inline constexpr auto Accent = "#2f6df6";
    }
    QString styleSheet();
}

#endif //MIXEDEDITING_THEME_H
