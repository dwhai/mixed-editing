//
// Created by Anlk on 2026/6/6.
// EditorWindow 实现。
//

#include "../include/EditorWindow.h"
#include "../include/theme.h"

#include "../../db/include/Database.h"
#include "../../ffmpeg/include/MediaProbe.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSqlQuery>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

namespace Mixed {

    namespace {
        // 微秒 → "mm:ss" 展示串。
        QString formatDurationUs(qint64 us) {
            const qint64 totalSec = us / 1'000'000;
            return QStringLiteral("%1:%2")
                .arg(totalSec / 60, 2, 10, QChar('0'))
                .arg(totalSec % 60, 2, 10, QChar('0'));
        }

        // 确保本地默认账户存在（project.account_id 有外键约束）。
        // 单用户场景下使用固定 id="local"；多用户登录后由真实账户替代。
        void ensureLocalAccount() {
            QSqlQuery q(DB::Database::instance().db());
            const qint64 now = DB::Database::nowMs();
            q.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO account(id,username,created_at,updated_at,rev) "
                "VALUES('local','本地用户',?,?,1)"));
            q.addBindValue(now);
            q.addBindValue(now);
            q.exec();
        }
    } // namespace

    EditorWindow::EditorWindow(QWidget *parent) : QMainWindow(parent) {
        setWindowTitle(QStringLiteral("剪辑工作区"));
        setStyleSheet(Theme::styleSheet());
        // 独立窗口：默认占据较大尺寸，剪辑需要充足的横向空间。
        resize(1280, 800);
        setupUi();
        // 惰性创建：进入时不建工程，等首次导入素材或添加片段时才落库，
        // 避免反复进出编辑器在库里堆积空工程。先渲染空时间线占位。
        refreshTimeline();
    }

    void EditorWindow::setupUi() {
        auto *root = new QWidget(this);
        root->setObjectName("editorRoot");
        setCentralWidget(root);

        auto *layout = new QVBoxLayout(root);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        layout->addWidget(buildTopBar());

        // 中部：左素材库(3) + 右预览区(7)
        auto *middle = new QHBoxLayout();
        middle->setContentsMargins(12, 12, 12, 12);
        middle->setSpacing(12);
        middle->addWidget(buildMediaPanel(), 3);
        middle->addWidget(buildPreviewArea(), 7);
        layout->addLayout(middle, /*stretch=*/1);

        layout->addWidget(buildTimeline());
    }

    QWidget *EditorWindow::buildTopBar() {
        auto *bar = new QFrame(this);
        bar->setObjectName("editorTopBar");

        auto *layout = new QHBoxLayout(bar);
        layout->setContentsMargins(16, 10, 16, 10);
        layout->setSpacing(12);

        auto *back = new QPushButton(QStringLiteral("‹  返回"), bar);
        back->setObjectName("editorBackButton");
        back->setCursor(Qt::PointingHandCursor);
        connect(back, &QPushButton::clicked, this, &EditorWindow::close);

        m_projectTitle = new QLabel(QStringLiteral("未命名工程"), bar);
        m_projectTitle->setObjectName("editorProjectTitle");

        auto *exportBtn = new QPushButton(QStringLiteral("导出"), bar);
        exportBtn->setObjectName("editorExportButton");
        exportBtn->setCursor(Qt::PointingHandCursor);

        layout->addWidget(back);
        layout->addStretch();
        layout->addWidget(m_projectTitle);
        layout->addStretch();
        layout->addWidget(exportBtn);
        return bar;
    }

    QWidget *EditorWindow::buildMediaPanel() {
        auto *panel = new QFrame(this);
        panel->setObjectName("editorMediaPanel");

        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(10);

        auto *heading = new QLabel(QStringLiteral("素材库"), panel);
        heading->setObjectName("editorPanelTitle");
        layout->addWidget(heading);

        auto *importBtn = new QPushButton(QStringLiteral("+  导入素材"), panel);
        importBtn->setObjectName("editorImportButton");
        importBtn->setCursor(Qt::PointingHandCursor);
        connect(importBtn, &QPushButton::clicked, this, &EditorWindow::importMedia);
        layout->addWidget(importBtn);

        // 素材列表：滚动容器，由 refreshMediaList() 动态填充。
        auto *scroll = new QScrollArea(panel);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);

        m_mediaList = new QWidget(scroll);
        m_mediaList->setObjectName("scrollContent");
        m_mediaListLayout = new QVBoxLayout(m_mediaList);
        m_mediaListLayout->setContentsMargins(0, 0, 0, 0);
        m_mediaListLayout->setSpacing(8);
        m_mediaListLayout->addStretch();
        scroll->setWidget(m_mediaList);

        layout->addWidget(scroll, /*stretch=*/1);
        return panel;
    }

    QWidget *EditorWindow::buildPreviewArea() {
        auto *area = new QFrame(this);
        area->setObjectName("editorPreviewArea");

        auto *layout = new QVBoxLayout(area);
        layout->setContentsMargins(0, 0, 0, 0);

        auto *canvas = new QLabel(QStringLiteral("预览区"), area);
        canvas->setObjectName("editorPreviewCanvas");
        canvas->setAlignment(Qt::AlignCenter);
        layout->addWidget(canvas, /*stretch=*/1);

        return area;
    }

    QWidget *EditorWindow::buildTimeline() {
        auto *timeline = new QFrame(this);
        timeline->setObjectName("editorTimeline");
        timeline->setMinimumHeight(200);

        auto *layout = new QVBoxLayout(timeline);
        layout->setContentsMargins(14, 12, 14, 14);
        layout->setSpacing(8);

        auto *heading = new QLabel(QStringLiteral("时间线"), timeline);
        heading->setObjectName("editorPanelTitle");
        layout->addWidget(heading);

        // 轨道容器：由 refreshTimeline() 按 track → clip 动态填充。
        m_timelineTracks = new QWidget(timeline);
        m_timelineTracks->setObjectName("scrollContent");
        m_timelineLayout = new QVBoxLayout(m_timelineTracks);
        m_timelineLayout->setContentsMargins(0, 0, 0, 0);
        m_timelineLayout->setSpacing(8);
        layout->addWidget(m_timelineTracks, /*stretch=*/1);

        return timeline;
    }

    // PLACEHOLDER_DATA

    void EditorWindow::ensureProject() {
        // 幂等：工程已存在则直接返回。
        if (!m_project.id.isEmpty()) {
            return;
        }
        if (!DB::Database::instance().isOpen()) {
            // 数据库不可用时退化为纯 UI（不落库），仍可浏览界面。
            return;
        }

        // 单用户场景：暂用固定本地账户 id（多用户登录后替换为当前账户）。
        ensureLocalAccount();
        m_project.accountId = QStringLiteral("local");
        m_project.title = QStringLiteral("未命名工程");
        if (!m_projectRepo.insert(m_project)) {
            m_project = DB::Project{}; // 插入失败回滚内存态，下次重试
            return;
        }
        if (m_projectTitle) {
            m_projectTitle->setText(m_project.title);
        }

        // 主序列。
        m_sequence.projectId = m_project.id;
        m_sequence.isMain = true;
        m_sequenceRepo.insert(m_sequence);

        // 默认两条轨道：视频(0) + 音频(1)。
        DB::Track videoTrack;
        videoTrack.sequenceId = m_sequence.id;
        videoTrack.trackType = QStringLiteral("video");
        videoTrack.trackIndex = 0;
        videoTrack.name = QStringLiteral("视频轨 1");
        m_trackRepo.insert(videoTrack);
        m_videoTrackId = videoTrack.id;

        DB::Track audioTrack;
        audioTrack.sequenceId = m_sequence.id;
        audioTrack.trackType = QStringLiteral("audio");
        audioTrack.trackIndex = 1;
        audioTrack.name = QStringLiteral("音频轨 1");
        m_trackRepo.insert(audioTrack);
        m_audioTrackId = audioTrack.id;

        refreshTimeline();
    }

    void EditorWindow::importMedia() {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("导入素材"), QString(),
            QStringLiteral("媒体文件 (*.mp4 *.mov *.mkv *.avi *.flv *.mp3 *.wav *.aac *.m4a "
                           "*.jpg *.jpeg *.png *.webp);;所有文件 (*)"));
        if (files.isEmpty()) {
            return;
        }

        // 首次导入时才真正创建工程。
        ensureProject();
        if (m_project.id.isEmpty()) {
            return; // 数据库未就绪
        }

        for (const QString &path : files) {
            const Player::MediaInfo info = Player::MediaProbe::probe(path);

            DB::MediaAsset asset;
            asset.projectId = m_project.id;
            asset.sourceKind = QStringLiteral("local");
            asset.filePath = path;
            asset.displayName = QFileInfo(path).fileName();

            // 依据探测结果归类媒体类型。
            if (info.hasVideo) {
                asset.mediaType = QStringLiteral("video");
            } else if (info.hasAudio) {
                asset.mediaType = QStringLiteral("audio");
            } else {
                asset.mediaType = QStringLiteral("image");
            }

            asset.durationUs = info.durationUs;
            asset.width = info.width;
            asset.height = info.height;
            asset.fps = info.fps;
            asset.sampleRate = info.sampleRate;
            asset.channels = info.channels;
            asset.codec = info.videoCodec;
            asset.fileSize = QFileInfo(path).size();

            m_assetRepo.insert(asset);
        }

        refreshMediaList();
    }

    void EditorWindow::addClipFromAsset(const DB::MediaAsset &asset) {
        ensureProject(); // 防御性：素材存在即工程已建，此处保证轨道就绪
        if (m_videoTrackId.isEmpty()) {
            return;
        }

        // 图片无内在时长，给一个默认 5 秒展示时长。
        const qint64 durationUs = asset.durationUs > 0 ? asset.durationUs : 5'000'000;
        const bool isAudio = asset.mediaType == QStringLiteral("audio");
        const QString trackId = isAudio ? m_audioTrackId : m_videoTrackId;

        // 追加到该轨已有片段末尾：起点 = 轨道现有片段总时长。
        qint64 startUs = 0;
        for (const DB::Clip &c : m_clipRepo.listByTrack(trackId)) {
            startUs = std::max(startUs, c.timelineStartUs + c.durationUs);
        }

        DB::Clip clip;
        clip.trackId = trackId;
        clip.assetId = asset.id;
        clip.kind = asset.mediaType;
        clip.timelineStartUs = startUs;
        clip.durationUs = durationUs;
        clip.sourceInUs = 0;
        clip.sourceOutUs = durationUs;
        m_clipRepo.insert(clip);

        // 同步序列总时长缓存。
        const qint64 clipEnd = startUs + durationUs;
        if (clipEnd > m_sequence.durationUs) {
            m_sequence.durationUs = clipEnd;
            m_sequenceRepo.update(m_sequence);
        }

        refreshTimeline();
    }

    void EditorWindow::refreshMediaList() {
        if (!m_mediaListLayout) {
            return;
        }
        // 清空除末尾 stretch 外的所有条目。
        while (m_mediaListLayout->count() > 1) {
            QLayoutItem *item = m_mediaListLayout->takeAt(0);
            if (QWidget *w = item->widget()) {
                w->deleteLater();
            }
            delete item;
        }

        const std::vector<DB::MediaAsset> assets = m_assetRepo.listByProject(m_project.id);
        if (assets.empty()) {
            auto *hint = new QLabel(QStringLiteral("暂无素材\n点击上方导入视频、音频或图片"), m_mediaList);
            hint->setObjectName("editorEmptyHint");
            hint->setAlignment(Qt::AlignCenter);
            hint->setWordWrap(true);
            m_mediaListLayout->insertWidget(0, hint);
            return;
        }

        for (const DB::MediaAsset &asset : assets) {
            // 每条素材为一个可点击卡片，点击即添加到时间线。
            auto *card = new QPushButton(m_mediaList);
            card->setObjectName("editorAssetCard");
            card->setCursor(Qt::PointingHandCursor);
            card->setText(QStringLiteral("%1\n%2 · %3")
                              .arg(asset.displayName,
                                   asset.mediaType,
                                   formatDurationUs(asset.durationUs)));
            const DB::MediaAsset captured = asset;
            connect(card, &QPushButton::clicked, this, [this, captured]() {
                addClipFromAsset(captured);
            });
            m_mediaListLayout->insertWidget(m_mediaListLayout->count() - 1, card);
        }
    }

    void EditorWindow::refreshTimeline() {
        if (!m_timelineLayout) {
            return;
        }
        while (m_timelineLayout->count() > 0) {
            QLayoutItem *item = m_timelineLayout->takeAt(0);
            if (QWidget *w = item->widget()) {
                w->deleteLater();
            }
            delete item;
        }

        const std::vector<DB::Track> tracks = m_trackRepo.listBySequence(m_sequence.id);
        if (tracks.empty()) {
            auto *hint = new QLabel(QStringLiteral("拖拽素材到此处开始剪辑"), m_timelineTracks);
            hint->setObjectName("editorTimelineHint");
            hint->setAlignment(Qt::AlignCenter);
            m_timelineLayout->addWidget(hint);
            return;
        }

        for (const DB::Track &track : tracks) {
            auto *row = new QFrame(m_timelineTracks);
            row->setObjectName("editorTrackRow");
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(8, 6, 8, 6);
            rowLayout->setSpacing(6);

            auto *label = new QLabel(track.name, row);
            label->setObjectName("editorTrackLabel");
            label->setFixedWidth(72);
            rowLayout->addWidget(label);

            const std::vector<DB::Clip> clips = m_clipRepo.listByTrack(track.id);
            for (const DB::Clip &clip : clips) {
                auto *block = new QLabel(formatDurationUs(clip.durationUs), row);
                block->setObjectName("editorClipBlock");
                block->setAlignment(Qt::AlignCenter);
                // 用时长按比例换算宽度（1 秒 ≈ 12px），保证可见最小宽度。
                const int width = std::max(40, static_cast<int>(clip.durationUs / 1'000'000 * 12));
                block->setFixedWidth(width);
                rowLayout->addWidget(block);
            }
            rowLayout->addStretch();
            m_timelineLayout->addWidget(row);
        }
        m_timelineLayout->addStretch();
    }


    void EditorWindow::closeEvent(QCloseEvent *event) {
        if (!m_closedEmitted) {
            m_closedEmitted = true;
            emit closed();
        }
        QMainWindow::closeEvent(event);
    }

    EditorWindow::~EditorWindow() = default;

} // namespace Mixed
