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
/* 容器背景透明：视频按宽高比居中铺放，多余区域并入页面背景，避免突兀黑边 */
QWidget#playerContainer { background-color: transparent; }
QWidget#relatedVideos { border-radius: 12px; background-color: #171a22; }

/* 悬浮控制条：覆盖在视频画面底部的半透明渐变条 */
QWidget#playerControlBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 rgba(0, 0, 0, 0%), stop:1 rgba(0, 0, 0, 80%));
}
QLabel#playerStatus { color: #e6ebf5; font-size: 12px; }
QLabel#playerTime { color: #e6ebf5; font-size: 12px; padding-left: 4px; }

QPushButton#playerIconButton {
    border: none; background: transparent; color: #f2f5fb;
    font-size: 17px; min-width: 30px; padding: 4px 4px;
}
QPushButton#playerIconButton:hover { color: #8fd2ff; }
QPushButton#playerIconButton:disabled { color: #6b7488; }

QPushButton#playerTextButton {
    border: none; background: transparent; color: #e6ebf5;
    font-size: 12px; padding: 4px 6px;
}
QPushButton#playerTextButton:hover { color: #8fd2ff; }
QPushButton#playerTextButton:disabled { color: #6b7488; }
/* 字幕开关：开启时高亮，关闭时灰显 */
QPushButton#playerTextButton[active="true"] { color: #8fd2ff; font-weight: 600; }
QPushButton#playerTextButton[active="false"] { color: #9aa4b8; }

/* 字幕叠加层 */
QLabel#playerSubtitle {
    color: #ffffff; font-size: 20px; font-weight: 600;
    background-color: rgba(0, 0, 0, 45%); padding: 2px 8px;
}

/* 音量弹出滑块 */
QWidget#volumePopup { border-radius: 8px; background-color: rgba(20, 22, 30, 92%); }
QSlider#volumeSlider::groove:vertical {
    width: 4px; border-radius: 2px; background: #3a4152;
}
QSlider#volumeSlider::sub-page:vertical { background: #3a4152; border-radius: 2px; }
QSlider#volumeSlider::add-page:vertical { background: #8fd2ff; border-radius: 2px; }
QSlider#volumeSlider::handle:vertical {
    height: 12px; width: 12px; margin: 0 -5px; border-radius: 6px; background: #eef4ff;
}
)");
    }
}
