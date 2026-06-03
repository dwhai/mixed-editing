//
// Implementation of the global style sheet.
//

#include "../include/theme.h"

namespace Mixed::Theme {
    QString styleSheet() {
        return QStringLiteral(R"(
/* ---- Window & generic containers ---- */
QMainWindow, QWidget#root { background-color: #111217; }
QScrollArea { border: none; background-color: transparent; }
QWidget#scrollContent { background-color: transparent; }

/* ---- Sidebar ---- */
QFrame#sidebar { border-radius: 14px; background-color: #171a22; }
QFrame#accountCard { border-radius: 10px; background-color: #1b1f2a; }
QLabel#accountLabel { color: #cdd5e6; font-size: 12px; }
QPushButton#vipButton {
    border: none; border-radius: 7px; background-color: #dbe8ff;
    color: #1a202c; font-size: 11px; font-weight: 600; padding: 7px 10px;
}
QPushButton#vipButton:hover { background-color: #edf4ff; }
QFrame#discoverCard { border-radius: 9px; background-color: #1c2231; }
QLabel#discoverText { color: #cfd6e6; font-size: 12px; }

/* ---- Navigation items (reusable via role property) ---- */
QPushButton[role="nav"] {
    text-align: left; border: none; border-radius: 8px; color: #d0d6e5;
    background-color: transparent; font-size: 13px; padding: 10px 12px;
}
QPushButton[role="nav"]:hover { background-color: #212636; }
QPushButton[role="navActive"] {
    text-align: left; border: none; border-radius: 8px; color: #f3f7ff;
    background-color: #252a38; font-size: 13px; font-weight: 600; padding: 10px 12px;
}

/* ---- Section titles ---- */
QLabel[role="sectionTitle"] { color: #dde3ef; font-size: 22px; font-weight: 650; }

/* ---- Top banner ---- */
QFrame#topBanner {
    border-radius: 18px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #2f3f80, stop:0.35 #365ea6, stop:0.7 #2f5f86, stop:1 #214044);
}
QPushButton#startButton {
    border: none; border-radius: 20px; background-color: rgba(0, 0, 0, 45%);
    color: #f4f7ff; font-size: 18px; font-weight: 600; padding: 8px 24px;
}
QPushButton#startButton:hover { background-color: rgba(255, 255, 255, 12%); }

/* ---- Function entries (reusable) ---- */
QLabel[role="funcIcon"] {
    min-width: 54px; max-width: 54px; min-height: 54px; max-height: 54px;
    border-radius: 27px; background-color: #262a34; color: #8fd2ff;
    font-size: 18px; font-weight: 700;
}
QLabel[role="funcText"] { color: #c8cfde; font-size: 13px; }

/* ---- Upgrade cards (reusable) ---- */
QFrame[role="card"] { border-radius: 14px; background-color: #1c1f27; }
QLabel[role="cardTitle"] { color: #eef2fa; font-size: 15px; font-weight: 600; }
QLabel[role="cardDesc"] { color: #9aa4b8; font-size: 12px; }

/* ---- Local draft card ---- */
QFrame#draftCard {
    min-width: 170px; max-width: 170px; min-height: 180px; max-height: 180px;
    border-radius: 14px; background-color: #1f232d;
}
QLabel#draftThumb {
    min-width: 126px; max-width: 126px; min-height: 126px; max-height: 126px;
    border-radius: 10px; background-color: #4f545f; color: #e3e7f0;
}
QLabel#draftDate { color: #e2e6f0; font-size: 12px; font-weight: 600; }
QLabel#draftInfo { color: #99a3b6; font-size: 11px; }

/* ---- Player page ---- */
QLabel#playerStatus { color: #c8cfde; font-size: 13px; }
QPushButton#playerNextButton {
    border: none; border-radius: 8px; background-color: #252a38;
    color: #eef2fa; font-size: 13px; font-weight: 600; padding: 8px 18px;
}
QPushButton#playerNextButton:hover { background-color: #2f3548; }
QPushButton#playerNextButton:disabled { color: #6b7488; background-color: #1c1f27; }
)");
    }
}
