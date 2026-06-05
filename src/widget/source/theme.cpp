//
// Implementation of the global style sheet.
//

#include "../include/theme.h"

namespace Mixed::Theme {
    QString styleSheet() {
        return QStringLiteral(R"(
/* ---- Window & generic containers (light theme) ---- */
QMainWindow, QWidget#root { background-color: #f4f5f7; }
QScrollArea { border: none; background-color: transparent; }
QWidget#scrollContent { background-color: transparent; }

/* ---- Top navigation bar ---- */
QFrame#topBar { border-radius: 14px; background-color: #ffffff; border: 1px solid #e6e8ec; }
QPushButton[role="topNav"] {
    border: none; border-radius: 8px; color: #5a6172;
    background-color: transparent; font-size: 14px; padding: 8px 18px;
}
QPushButton[role="topNav"]:hover { color: #1a1d24; background-color: #eef0f4; }
QPushButton[role="topNavActive"] {
    border: none; border-radius: 8px; color: #2f6df6;
    background-color: #e8eefc; font-size: 14px; font-weight: 600; padding: 8px 18px;
}
QPushButton#accountButton {
    border: none; border-radius: 8px; background-color: #eef0f4;
    color: #5a6172; font-size: 12px; padding: 8px 14px;
}
QPushButton#accountButton:hover { background-color: #e2e5ea; color: #1a1d24; }

/* ---- Sidebar (legacy account/discover cards, kept for reuse) ---- */
QFrame#sidebar { border-radius: 14px; background-color: #ffffff; border: 1px solid #e6e8ec; }
QFrame#accountCard { border-radius: 10px; background-color: #f4f5f7; }
QLabel#accountLabel { color: #5a6172; font-size: 12px; }
QPushButton#vipButton {
    border: none; border-radius: 7px; background-color: #2f6df6;
    color: #ffffff; font-size: 11px; font-weight: 600; padding: 7px 10px;
}
QPushButton#vipButton:hover { background-color: #4b82f8; }
QFrame#discoverCard { border-radius: 9px; background-color: #eef2fb; }
QLabel#discoverText { color: #5a6172; font-size: 12px; }

/* ---- Navigation items (reusable via role property) ---- */
QPushButton[role="nav"] {
    text-align: left; border: none; border-radius: 8px; color: #5a6172;
    background-color: transparent; font-size: 13px; padding: 10px 12px;
}
QPushButton[role="nav"]:hover { background-color: #eef0f4; }
QPushButton[role="navActive"] {
    text-align: left; border: none; border-radius: 8px; color: #2f6df6;
    background-color: #e8eefc; font-size: 13px; font-weight: 600; padding: 10px 12px;
}

/* ---- Video list grid (短剧 page) ---- */
QWidget#videoGrid { background-color: transparent; }
QLabel#listStatus { color: #8b92a3; font-size: 14px; padding: 40px; }
QFrame#videoCard { border-radius: 12px; background-color: transparent; }
QFrame#videoCard:hover { background-color: #ffffff; }
QLabel#cardThumb { border-radius: 10px; background-color: #e6e8ec; }
QLabel#cardDuration {
    color: #ffffff; font-size: 11px; font-weight: 600;
    background-color: rgba(0, 0, 0, 65%); border-radius: 4px; padding: 1px 6px;
}
QLabel#cardTitle { color: #1a1d24; font-size: 13px; font-weight: 600; }
QLabel#cardAuthor { color: #8b92a3; font-size: 12px; }

/* ---- Section titles ---- */
QLabel[role="sectionTitle"] { color: #1a1d24; font-size: 22px; font-weight: 650; }

/* ---- Top banner ---- */
QFrame#topBanner {
    border-radius: 18px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #4b6ef5, stop:0.5 #5b8def, stop:1 #6aa6e8);
}
QPushButton#startButton {
    border: none; border-radius: 20px; background-color: rgba(255, 255, 255, 25%);
    color: #ffffff; font-size: 18px; font-weight: 600; padding: 8px 24px;
}
QPushButton#startButton:hover { background-color: rgba(255, 255, 255, 38%); }

/* ---- Function entries (reusable) ---- */
QLabel[role="funcIcon"] {
    min-width: 54px; max-width: 54px; min-height: 54px; max-height: 54px;
    border-radius: 27px; background-color: #e8eefc; color: #2f6df6;
    font-size: 18px; font-weight: 700;
}
QLabel[role="funcText"] { color: #5a6172; font-size: 13px; }

/* ---- Upgrade cards (reusable) ---- */
QFrame[role="card"] { border-radius: 14px; background-color: #ffffff; border: 1px solid #e6e8ec; }
QLabel[role="cardTitle"] { color: #1a1d24; font-size: 15px; font-weight: 600; }
QLabel[role="cardDesc"] { color: #8b92a3; font-size: 12px; }

/* ---- Local draft card ---- */
QFrame#draftCard {
    min-width: 170px; max-width: 170px; min-height: 180px; max-height: 180px;
    border-radius: 14px; background-color: #ffffff; border: 1px solid #e6e8ec;
}
QLabel#draftThumb {
    min-width: 126px; max-width: 126px; min-height: 126px; max-height: 126px;
    border-radius: 10px; background-color: #d7dae0; color: #5a6172;
}
QLabel#draftDate { color: #1a1d24; font-size: 12px; font-weight: 600; }
QLabel#draftInfo { color: #8b92a3; font-size: 11px; }

/* ---- Player page ---- */
/* 播放器容器用深炭灰而非纯黑：视频按宽高比居中铺放，上下黑边不至于死黑突兀 */
QWidget#playerContainer { background-color: #15171c; border-radius: 10px; }
QDialog#playerDialog { background-color: #f4f5f7; }
QWidget#relatedVideos { border-radius: 12px; background-color: #ffffff; border: 1px solid #e6e8ec; }
QWidget#relatedContent { background-color: transparent; }
QLabel#relatedTitle { color: #1a1d24; font-size: 15px; font-weight: 600; }

/* 顶部信息区 */
QLabel#videoTitle { color: #1a1d24; font-size: 18px; font-weight: 650; }
QLabel#videoStats { color: #8b92a3; font-size: 12px; }
QLabel#authorName { color: #1a1d24; font-size: 14px; font-weight: 600; }
QLabel#authorDesc { color: #8b92a3; font-size: 12px; }

/* 视频底部进度条（细线，仅展示进度） */
QProgressBar#playerProgress {
    border: none; background-color: rgba(255, 255, 255, 25%);
}
QProgressBar#playerProgress::chunk { background-color: #2f6df6; }

/* 悬浮控制条：覆盖在视频画面底部的半透明渐变条（叠在黑色画面上，保持浅色文字） */
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
