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
    // Core palette. Reuse these constants instead of hard-coding hex strings.
    namespace Color {
        inline constexpr auto Background = "#111217";
        inline constexpr auto Sidebar = "#171a22";
        inline constexpr auto Card = "#1c1f27";
        inline constexpr auto AccountCard = "#1b1f2a";
        inline constexpr auto DraftCard = "#1f232d";
        inline constexpr auto NavActive = "#252a38";
        inline constexpr auto NavHover = "#212636";

        inline constexpr auto TextPrimary = "#eef2fa";
        inline constexpr auto TextSecondary = "#c8cfde";
        inline constexpr auto TextMuted = "#9aa4b8";
        inline constexpr auto Accent = "#8fd2ff";
    }
    QString styleSheet();
}

#endif //MIXEDEDITING_THEME_H
