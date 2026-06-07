//
// Created by Anlk on 2026/6/6.
// EditorWindow 实现。
//

#include "../include/EditorWindow.h"
#include "../include/theme.h"
#include "../include/TimelineView.h"
#include "../include/VideoGLWidget.h"

#include "../../db/include/Database.h"
#include "../../ffmpeg/include/MediaProbe.h"
#include "../../ffmpeg/include/CompositionEngine.h"
#include "../../ffmpeg/include/Exporter.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMetaType>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "../../ffmpeg/include/ClipSource.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace Mixed {

    namespace {
        // 微秒 → "mm:ss" 展示串。
        QString formatDurationUs(qint64 us) {
            const qint64 totalSec = us / 1'000'000;
            return QStringLiteral("%1:%2")
                .arg(totalSec / 60, 2, 10, QChar('0'))
                .arg(totalSec % 60, 2, 10, QChar('0'));
        }

        // 素材卡片副标题：格式（容器扩展名 + 编码）+ 分辨率 + 总时长。
        // 视频/音频显示总时长；图片无内在时长，改显分辨率不带时长。
        QString assetMetaLine(const DB::MediaAsset &asset) {
            QStringList parts;

            // 容器格式：取文件扩展名（大写），如 MP4 / MOV / PNG。
            const QString ext = QFileInfo(asset.filePath).suffix().toUpper();
            if (!ext.isEmpty()) {
                parts << ext;
            }
            // 编码名（探测得到时）。
            if (!asset.codec.isEmpty()) {
                parts << asset.codec;
            }
            // 分辨率（有画面的素材）。
            if (asset.width > 0 && asset.height > 0) {
                parts << QStringLiteral("%1×%2").arg(asset.width).arg(asset.height);
            }
            // 总时长（图片无时长则跳过）。
            if (asset.durationUs > 0) {
                parts << formatDurationUs(asset.durationUs);
            }

            return parts.join(QStringLiteral("  ·  "));
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

    // ============ ThumbnailWorker（worker 线程） ============
    ThumbnailWorker::ThumbnailWorker(QObject *parent) : QObject(parent) {}
    ThumbnailWorker::~ThumbnailWorker() = default;

    void ThumbnailWorker::decodeClip(quint64 reqGen, const QString &clipId,
                                     const QString &filePath, qint64 sourceInUs,
                                     qint64 sourceOutUs, int tileH) {
        // 代际过期：本次请求已被更新一轮重建取代，直接放弃（不解码、不发结果）。
        if (reqGen != m_generation.load()) {
            return;
        }
        // 代际切换：清空上一轮的解码器缓存，释放文件句柄。
        if (m_cacheGen != reqGen) {
            m_sources.clear();
            m_cacheGen = reqGen;
        }
        if (filePath.isEmpty()) {
            return;
        }

        // 取（或创建）该文件的解码器，跨片段复用。
        const std::string key = filePath.toStdString();
        auto it = m_sources.find(key);
        if (it == m_sources.end()) {
            auto src = std::make_unique<Player::ClipSource>();
            if (!src->open(filePath)) {
                m_sources.emplace(key, nullptr); // 记下失败，避免重复尝试
                return;
            }
            it = m_sources.emplace(key, std::move(src)).first;
        }
        if (!it->second) {
            return;
        }
        Player::ClipSource *src = it->second.get();

        // 采样格数：按片段时长每 ~2 秒一格，钳制在 [1, 12]。
        const qint64 spanUs = std::max<qint64>(1, sourceOutUs - sourceInUs);
        int n = static_cast<int>(spanUs / 2'000'000) + 1;
        n = std::clamp(n, 1, 12);

        QVector<QImage> frames;
        frames.reserve(n);
        for (int k = 0; k < n; ++k) {
            // 解码途中代际若失效，立即中止本片段剩余帧。
            if (reqGen != m_generation.load()) {
                return;
            }
            const qint64 sampleUs =
                sourceInUs + static_cast<qint64>((k + 0.5) / n * spanUs);
            QImage full = src->frameAt(sampleUs);
            if (!full.isNull()) {
                frames.push_back(full.scaledToHeight(tileH, Qt::SmoothTransformation));
            }
        }
        if (!frames.isEmpty() && reqGen == m_generation.load()) {
            emit clipReady(reqGen, clipId, frames);
        }
    }

    void ThumbnailWorker::decodeWaveform(quint64 reqGen, const QString &clipId,
                                         const QString &filePath, qint64 sourceInUs,
                                         qint64 sourceOutUs, int buckets) {
        if (reqGen != m_generation.load() || filePath.isEmpty() || buckets <= 0) {
            return;
        }

        // 独立打开文件解码音频（不复用视频 ClipSource，二者解码器不同）。
        // 失败则静默返回——音轨退回纯色块。
        AVFormatContext *fmt = nullptr;
        if (avformat_open_input(&fmt, filePath.toUtf8().constData(), nullptr, nullptr) != 0) {
            return;
        }
        if (avformat_find_stream_info(fmt, nullptr) < 0) {
            avformat_close_input(&fmt);
            return;
        }
        int audioIdx = -1;
        for (unsigned i = 0; i < fmt->nb_streams; ++i) {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audioIdx = static_cast<int>(i);
                break;
            }
        }
        if (audioIdx < 0) {
            avformat_close_input(&fmt);
            return;
        }

        AVStream *stream = fmt->streams[audioIdx];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!ctx || avcodec_parameters_to_context(ctx, stream->codecpar) < 0 ||
            avcodec_open2(ctx, codec, nullptr) < 0) {
            if (ctx) avcodec_free_context(&ctx);
            avformat_close_input(&fmt);
            return;
        }

        // 重采样为单声道 float（便于求峰值），采样率沿用源。
        SwrContext *swr = nullptr;
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, 1);
        swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, ctx->sample_rate,
                            &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
        if (!swr || swr_init(swr) < 0) {
            if (swr) swr_free(&swr);
            avcodec_free_context(&ctx);
            avformat_close_input(&fmt);
            return;
        }

        const int sampleRate = ctx->sample_rate > 0 ? ctx->sample_rate : 48000;
        // 桶时间宽度：把 [sourceIn, sourceOut] 均分成 buckets 段，每段记最大幅值。
        const qint64 spanUs = std::max<qint64>(1, sourceOutUs - sourceInUs);
        QVector<float> peaks(buckets, 0.0f);

        // seek 到源起点附近。
        const int64_t seekTs = av_rescale_q(sourceInUs, AVRational{1, 1000000}, stream->time_base);
        av_seek_frame(fmt, audioIdx, seekTs, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(ctx);

        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        std::vector<float> buf;
        bool aborted = false;

        while (!aborted && av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == audioIdx && avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    if (reqGen != m_generation.load()) { aborted = true; break; }

                    // 该帧的时间线位置（微秒，源时间）。
                    const int64_t bestPts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                        ? frame->best_effort_timestamp : frame->pts;
                    const qint64 framePtsUs = (bestPts == AV_NOPTS_VALUE) ? sourceInUs
                        : av_rescale_q(bestPts, stream->time_base, AVRational{1, 1000000});
                    if (framePtsUs >= sourceOutUs) { aborted = true; break; }

                    // 重采样到 float 单声道。
                    const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
                    buf.resize(static_cast<size_t>(std::max(0, maxOut)));
                    uint8_t *outPtr = reinterpret_cast<uint8_t *>(buf.data());
                    const int got = swr_convert(swr, &outPtr, maxOut,
                        const_cast<const uint8_t **>(frame->data), frame->nb_samples);
                    for (int s = 0; s < got; ++s) {
                        // 该采样的时间线位置 → 桶下标。
                        const qint64 sampleUs = framePtsUs +
                            static_cast<qint64>(s * 1'000'000LL / sampleRate);
                        const qint64 rel = sampleUs - sourceInUs;
                        if (rel < 0 || rel >= spanUs) continue;
                        int b = static_cast<int>(rel * buckets / spanUs);
                        b = std::clamp(b, 0, buckets - 1);
                        const float amp = std::fabs(buf[static_cast<size_t>(s)]);
                        if (amp > peaks[b]) peaks[b] = amp;
                    }
                }
            }
            av_packet_unref(pkt);
        }

        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        av_channel_layout_uninit(&outLayout);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);

        if (reqGen == m_generation.load()) {
            emit waveformReady(reqGen, clipId, peaks);
        }
    }

    EditorWindow::EditorWindow(QWidget *parent) : QMainWindow(parent) {
        setWindowTitle(QStringLiteral("剪辑工作区"));
        setStyleSheet(Theme::styleSheet());
        // 独立窗口：默认占据较大尺寸，剪辑需要充足的横向空间。
        resize(1280, 800);

        m_engine = std::make_unique<Player::CompositionEngine>();

        // 缩略图/波形后台解码线程：QVector<QImage>/QVector<float> 注册为元类型以走队列连接。
        qRegisterMetaType<QVector<QImage>>("QVector<QImage>");
        qRegisterMetaType<QVector<float>>("QVector<float>");
        m_thumbThread = new QThread(this);
        m_thumbWorker = new ThumbnailWorker;        // 无父对象，随后 moveToThread
        m_thumbWorker->moveToThread(m_thumbThread);
        connect(this, &EditorWindow::requestThumbnail,
                m_thumbWorker, &ThumbnailWorker::decodeClip, Qt::QueuedConnection);
        connect(this, &EditorWindow::requestWaveform,
                m_thumbWorker, &ThumbnailWorker::decodeWaveform, Qt::QueuedConnection);
        connect(m_thumbWorker, &ThumbnailWorker::clipReady,
                this, &EditorWindow::onThumbnailReady, Qt::QueuedConnection);
        connect(m_thumbWorker, &ThumbnailWorker::waveformReady,
                this, &EditorWindow::onWaveformReady, Qt::QueuedConnection);
        m_thumbThread->start();

        setupUi();
        // 进入编辑器即尝试加载上次的工程（仅加载不创建），恢复工作空间；
        // 没有任何已有工程时渲染空时间线占位，等首次导入/添加再落库建工程。
        if (!loadExistingProject()) {
            rebuildComposition();
        }
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

        // 逐帧 + 时间码：上一帧 / 时间码 / 下一帧。
        auto *prevFrame = new QPushButton(QStringLiteral("◀|"), bar);
        prevFrame->setObjectName("editorStepButton");
        prevFrame->setCursor(Qt::PointingHandCursor);
        prevFrame->setToolTip(QStringLiteral("上一帧"));
        connect(prevFrame, &QPushButton::clicked, this, [this]() { stepFrame(-1); });

        m_timecodeLabel = new QLabel(formatTimecode(0), bar);
        m_timecodeLabel->setObjectName("editorTimecode");

        auto *nextFrame = new QPushButton(QStringLiteral("|▶"), bar);
        nextFrame->setObjectName("editorStepButton");
        nextFrame->setCursor(Qt::PointingHandCursor);
        nextFrame->setToolTip(QStringLiteral("下一帧"));
        connect(nextFrame, &QPushButton::clicked, this, [this]() { stepFrame(1); });

        auto *exportBtn = new QPushButton(QStringLiteral("导出"), bar);
        exportBtn->setObjectName("editorExportButton");
        exportBtn->setCursor(Qt::PointingHandCursor);
        connect(exportBtn, &QPushButton::clicked, this, &EditorWindow::exportProject);

        layout->addWidget(back);
        layout->addStretch();
        layout->addWidget(m_projectTitle);
        layout->addSpacing(16);
        layout->addWidget(prevFrame);
        layout->addWidget(m_timecodeLabel);
        layout->addWidget(nextFrame);
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

        auto *tip = new QLabel(QStringLiteral("双击添加到时间线 · 右键更多操作"), panel);
        tip->setObjectName("editorHintLine");
        layout->addWidget(tip);

        // 素材列表：QListWidget，每项一张卡片。
        // 双击 → 添加到时间线；右键 → 上下文菜单（添加 / 删除）。
        m_mediaList = new QListWidget(panel);
        m_mediaList->setObjectName("editorMediaList");
        m_mediaList->setFrameShape(QFrame::NoFrame);
        m_mediaList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_mediaList->setSelectionMode(QAbstractItemView::SingleSelection);
        m_mediaList->setContextMenuPolicy(Qt::CustomContextMenu);
        m_mediaList->setSpacing(4);

        connect(m_mediaList, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem *item) {
                    const QString id = item->data(Qt::UserRole).toString();
                    const std::optional<DB::MediaAsset> asset = m_assetRepo.findById(id);
                    if (asset) {
                        addClipFromAsset(*asset);
                    }
                });
        connect(m_mediaList, &QListWidget::customContextMenuRequested, this,
                [this](const QPoint &pos) {
                    QListWidgetItem *item = m_mediaList->itemAt(pos);
                    if (!item) return;
                    const QString id = item->data(Qt::UserRole).toString();
                    const std::optional<DB::MediaAsset> asset = m_assetRepo.findById(id);
                    if (asset) {
                        showMediaContextMenu(*asset, m_mediaList->viewport()->mapToGlobal(pos));
                    }
                });

        layout->addWidget(m_mediaList, /*stretch=*/1);
        return panel;
    }

    QWidget *EditorWindow::buildPreviewArea() {
        auto *area = new QFrame(this);
        area->setObjectName("editorPreviewArea");

        auto *layout = new QVBoxLayout(area);
        layout->setContentsMargins(0, 0, 0, 0);

        // 预览输出：合成引擎产出的 YUV420P 帧由 GL 控件渲染。
        m_preview = new Player::VideoGLWidget(area);
        layout->addWidget(m_preview, /*stretch=*/1);

        return area;
    }

    QWidget *EditorWindow::buildTimeline() {
        auto *timeline = new QFrame(this);
        timeline->setObjectName("editorTimeline");
        timeline->setMinimumHeight(220);

        auto *layout = new QVBoxLayout(timeline);
        layout->setContentsMargins(14, 12, 14, 14);
        layout->setSpacing(8);

        auto *heading = new QLabel(QStringLiteral("时间线  ·  Ctrl+滚轮缩放"), timeline);
        heading->setObjectName("editorPanelTitle");
        layout->addWidget(heading);

        // 自绘时间线控件放入横向滚动区（轨道可能超出可视宽度）。
        auto *scroll = new QScrollArea(timeline);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setObjectName("editorTimelineScroll");
        // widgetResizable=true：滚动区按 TimelineView 的最小尺寸决定滚动范围，
        // 内容增长/缩放后即可横向滚到后段（配合 updateScrollExtent 刷新最小尺寸）。
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_timelineScroll = scroll;

        m_timeline = new TimelineView(scroll);
        connect(m_timeline, &TimelineView::playheadMoved, this, &EditorWindow::seekTo);
        connect(m_timeline, &TimelineView::clipSelected, this, [this](const QString &clipId) {
            // 点选片段时跳到片段起点。
            const std::optional<DB::Clip> clip = m_clipRepo.findById(clipId);
            if (clip) {
                seekTo(clip->timelineStartUs);
            }
        });
        connect(m_timeline, &TimelineView::clipMoved, this, &EditorWindow::moveClip);
        connect(m_timeline, &TimelineView::clipDeleteRequested, this, &EditorWindow::deleteClip);
        scroll->setWidget(m_timeline);

        layout->addWidget(scroll, /*stretch=*/1);
        return timeline;
    }

    // PLACEHOLDER_DATA

    bool EditorWindow::loadExistingProject() {
        // 仅加载已有工程（不创建）。供进入编辑器时自动恢复上次工作空间。
        if (!m_project.id.isEmpty()) {
            return true; // 已加载
        }
        if (!DB::Database::instance().isOpen()) {
            return false;
        }
        ensureLocalAccount();

        const std::vector<DB::Project> existing =
            m_projectRepo.listByAccount(QStringLiteral("local"));
        if (existing.empty()) {
            return false; // 无已有工程
        }

        // 加载最新工程（listByAccount 已按 updated_at 倒序）。
        m_project = existing.front();
        if (m_projectTitle) {
            m_projectTitle->setText(m_project.title);
        }

        // 加载主序列。
        const std::vector<DB::Sequence> sequences = m_sequenceRepo.listByProject(m_project.id);
        for (const DB::Sequence &seq : sequences) {
            if (seq.isMain) {
                m_sequence = seq;
                break;
            }
        }

        // 加载轨道。
        if (!m_sequence.id.isEmpty()) {
            const std::vector<DB::Track> tracks = m_trackRepo.listBySequence(m_sequence.id);
            for (const DB::Track &track : tracks) {
                if (track.trackType == QStringLiteral("video")) {
                    m_videoTrackId = track.id;
                } else if (track.trackType == QStringLiteral("audio")) {
                    m_audioTrackId = track.id;
                }
            }
        }

        refreshMediaList();
        rebuildComposition();
        return true;
    }

    void EditorWindow::ensureProject() {
        // 幂等：工程已加载则直接返回。
        if (!m_project.id.isEmpty()) {
            return;
        }
        if (!DB::Database::instance().isOpen()) {
            // 数据库不可用时退化为纯 UI（不落库），仍可浏览界面。
            return;
        }

        // 已有工程则加载并返回。
        if (loadExistingProject()) {
            return;
        }

        // 单用户场景：暂用固定本地账户 id（多用户登录后替换为当前账户）。
        ensureLocalAccount();

        // 无已有工程，创建新的。
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

        rebuildComposition();
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

        // 已入库素材的路径集合：同一文件不重复落库（按 file_path 去重）。
        std::unordered_set<QString> existingPaths;
        for (const DB::MediaAsset &a : m_assetRepo.listByProject(m_project.id)) {
            if (!a.filePath.isEmpty()) {
                existingPaths.insert(a.filePath);
            }
        }

        for (const QString &path : files) {
            // 同一工程内已导入过的文件直接跳过，避免重复素材堆积。
            if (!existingPaths.insert(path).second) {
                continue;
            }

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

        // 含音频的视频：在音频轨同步建一个等长、对齐的音频片段（同一素材），
        // 这样音轨不再为空，并可绘制其波形。纯音频/图片不触发。
        const bool videoHasAudio = !isAudio &&
            asset.mediaType == QStringLiteral("video") && asset.channels > 0;
        if (videoHasAudio && !m_audioTrackId.isEmpty()) {
            // 音频片段对齐到视频片段同一起点（按视频轨末尾计算的 startUs）。
            DB::Clip audioClip;
            audioClip.trackId = m_audioTrackId;
            audioClip.assetId = asset.id;
            audioClip.kind = QStringLiteral("audio");
            audioClip.timelineStartUs = startUs;
            audioClip.durationUs = durationUs;
            audioClip.sourceInUs = 0;
            audioClip.sourceOutUs = durationUs;
            m_clipRepo.insert(audioClip);
        }

        // 同步序列总时长缓存。
        const qint64 clipEnd = startUs + durationUs;
        if (clipEnd > m_sequence.durationUs) {
            m_sequence.durationUs = clipEnd;
            m_sequenceRepo.update(m_sequence);
        }

        rebuildComposition();
        // 添加片段后自动跳到该片段起点，让用户立即看到画面。
        seekTo(startUs);
    }

    void EditorWindow::refreshMediaList() {
        if (!m_mediaList) {
            return;
        }
        m_mediaList->clear();

        const std::vector<DB::MediaAsset> assets = m_assetRepo.listByProject(m_project.id);
        if (assets.empty()) {
            auto *empty = new QListWidgetItem(
                QStringLiteral("暂无素材\n点击上方导入视频、音频或图片"), m_mediaList);
            empty->setFlags(Qt::NoItemFlags); // 占位提示，不可选/不可交互
            empty->setTextAlignment(Qt::AlignCenter);
            return;
        }

        // 显示去重：同一 file_path 只展示一张卡片（防止历史遗留的重复素材撑出多条）。
        std::unordered_set<QString> seenPaths;
        for (const DB::MediaAsset &asset : assets) {
            // 本地素材按路径去重；远程/无路径素材按 id 区分，正常展示。
            if (!asset.filePath.isEmpty() && !seenPaths.insert(asset.filePath).second) {
                continue;
            }

            auto *item = new QListWidgetItem(
                QStringLiteral("%1\n%2").arg(asset.displayName, assetMetaLine(asset)),
                m_mediaList);
            // 用 id 关联回数据行；双击/右键时据此取回 MediaAsset。
            item->setData(Qt::UserRole, asset.id);
            item->setToolTip(asset.filePath);
        }
    }

    void EditorWindow::showMediaContextMenu(const DB::MediaAsset &asset,
                                            const QPoint &globalPos) {
        QMenu menu(this);
        QAction *addAct = menu.addAction(QStringLiteral("添加到操作空间（时间线）"));
        menu.addSeparator();
        QAction *delAct = menu.addAction(QStringLiteral("从素材库删除"));

        const QAction *chosen = menu.exec(globalPos);
        if (chosen == addAct) {
            addClipFromAsset(asset);
        } else if (chosen == delAct) {
            deleteAsset(asset);
        }
    }

    void EditorWindow::deleteAsset(const DB::MediaAsset &asset) {
        if (asset.id.isEmpty() || m_sequence.id.isEmpty()) {
            // 工程未建则只可能有内存态，直接刷新即可。
            if (!asset.id.isEmpty()) m_assetRepo.remove(asset.id);
            refreshMediaList();
            return;
        }

        // 同一文件可能存在多条素材行（历史遗留）：按路径找出全部，连同其片段一并删除，
        // 避免删掉一条后另一条仍在库里、且时间线残留引用空素材的坏片段。
        std::unordered_set<QString> assetIds;
        for (const DB::MediaAsset &a : m_assetRepo.listByProject(m_project.id)) {
            if (a.id == asset.id ||
                (!asset.filePath.isEmpty() && a.filePath == asset.filePath)) {
                assetIds.insert(a.id);
            }
        }
        assetIds.insert(asset.id);

        // 级联软删除：所有引用这些素材的时间线片段。
        const std::vector<DB::Track> tracks = m_trackRepo.listBySequence(m_sequence.id);
        for (const DB::Track &track : tracks) {
            for (const DB::Clip &clip : m_clipRepo.listByTrack(track.id)) {
                if (assetIds.count(clip.assetId) > 0) {
                    m_clipRepo.remove(clip.id);
                }
            }
        }
        // 删除素材行本身。
        for (const QString &id : assetIds) {
            m_assetRepo.remove(id);
        }

        // 重算序列总时长（取剩余片段的最大结束点）。
        qint64 maxEnd = 0;
        for (const DB::Track &track : tracks) {
            for (const DB::Clip &clip : m_clipRepo.listByTrack(track.id)) {
                maxEnd = std::max(maxEnd, clip.timelineStartUs + clip.durationUs);
            }
        }
        if (maxEnd != m_sequence.durationUs) {
            m_sequence.durationUs = maxEnd;
            m_sequenceRepo.update(m_sequence);
        }

        refreshMediaList();
        rebuildComposition();
    }

    void EditorWindow::rebuildComposition() {
        if (!m_engine) {
            return;
        }
        m_engine->setCanvasSize(m_project.width > 0 ? m_project.width : 1920,
                                m_project.height > 0 ? m_project.height : 1080);

        // 工程未建（尚未导入任何素材）：清空时间线控件与画面。
        if (m_sequence.id.isEmpty()) {
            m_engine->setTimeline({}, {}, {});
            if (m_timeline) m_timeline->setContent({}, {}, 0);
            if (m_preview) m_preview->clearFrame();
            return;
        }

        // 读取轨道、各轨片段、以及 clip→源文件路径映射。
        const std::vector<DB::Track> tracks = m_trackRepo.listBySequence(m_sequence.id);
        std::vector<std::vector<DB::Clip>> clipsPerTrack;
        std::unordered_map<QString, std::vector<DB::Clip>> clipsByTrack;
        std::unordered_map<QString, QString> assetPathByClip;

        clipsPerTrack.reserve(tracks.size());
        for (const DB::Track &track : tracks) {
            std::vector<DB::Clip> clips = m_clipRepo.listByTrack(track.id);
            for (const DB::Clip &clip : clips) {
                const QString path = assetPathForClip(clip);
                if (!path.isEmpty()) {
                    assetPathByClip.emplace(clip.id, path);
                }
            }
            clipsByTrack.emplace(track.id, clips);
            clipsPerTrack.push_back(std::move(clips));
        }

        m_engine->setTimeline(tracks, clipsByTrack, assetPathByClip);
        if (m_timeline) {
            m_timeline->setContent(tracks, clipsPerTrack, m_sequence.durationUs);
        }
        refreshClipThumbnails();
        // 重建后刷新当前播放头画面。
        seekTo(m_playheadUs);
    }

    void EditorWindow::refreshClipThumbnails() {
        if (!m_timeline || m_sequence.id.isEmpty()) {
            return;
        }
        // 不在主线程解码：先清空旧缩略图（先显示纯色块），再为每个视频片段
        // 派发一个解码请求到 worker 线程。代际号自增，使上一轮未完成的请求作废。
        ++m_thumbGeneration;
        if (m_thumbWorker) {
            m_thumbWorker->setGeneration(m_thumbGeneration);
        }
        m_timeline->clearClipThumbnails();
        m_timeline->clearClipWaveforms();

        // 预缩放目标高度 = 时间线片段块绘制高度（kTrackH-8=48）。
        constexpr int kTileH = 48;
        // 波形桶数：固定分辨率，绘制时按块宽映射（与缩放无关）。
        constexpr int kWaveBuckets = 1200;

        const std::vector<DB::Track> tracks = m_trackRepo.listBySequence(m_sequence.id);
        for (const DB::Track &track : tracks) {
            const bool isVideoTrack = (track.trackType == QStringLiteral("video"));
            const bool isAudioTrack = (track.trackType == QStringLiteral("audio"));
            if (!isVideoTrack && !isAudioTrack) {
                continue;
            }
            for (const DB::Clip &clip : m_clipRepo.listByTrack(track.id)) {
                if (clip.assetId.isEmpty()) continue;
                const QString path = assetPathForClip(clip);
                if (path.isEmpty()) continue;

                if (clip.kind == QStringLiteral("audio")) {
                    // 音频片段：派发波形解码（在 worker 线程），完成回 onWaveformReady。
                    emit requestWaveform(m_thumbGeneration, clip.id, path,
                                         clip.sourceInUs, clip.sourceOutUs, kWaveBuckets);
                } else {
                    // 视频/图片片段：派发胶片缩略解码，完成回 onThumbnailReady。
                    emit requestThumbnail(m_thumbGeneration, clip.id, path,
                                          clip.sourceInUs, clip.sourceOutUs, kTileH);
                }
            }
        }
    }

    void EditorWindow::onThumbnailReady(quint64 reqGen, const QString &clipId,
                                        const QVector<QImage> &frames) {
        // 仅采纳当前代际的结果；过期结果（已被新一轮重建取代）丢弃。
        if (reqGen != m_thumbGeneration || !m_timeline) {
            return;
        }
        m_timeline->setClipThumbnail(clipId, frames);
    }

    void EditorWindow::onWaveformReady(quint64 reqGen, const QString &clipId,
                                       const QVector<float> &peaks) {
        if (reqGen != m_thumbGeneration || !m_timeline) {
            return;
        }
        m_timeline->setClipWaveform(clipId, peaks);
    }

    void EditorWindow::deleteClip(const QString &clipId) {
        const std::optional<DB::Clip> clip = m_clipRepo.findById(clipId);
        if (!clip) {
            return;
        }

        // 删除前弹确认框。
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("删除片段"));
        box.setText(QStringLiteral("确定从时间线删除该片段吗？"));
        box.setInformativeText(QStringLiteral("此操作仅移除时间线上的片段，不会删除素材库中的源文件。"));
        QPushButton *ok = box.addButton(QStringLiteral("删除"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        box.setIcon(QMessageBox::Warning);
        box.exec();
        if (box.clickedButton() != ok) {
            return; // 用户取消
        }

        m_clipRepo.remove(clipId);

        // 重算序列总时长（取剩余片段最大结束点）。
        qint64 maxEnd = 0;
        const std::vector<DB::Track> tracks = m_trackRepo.listBySequence(m_sequence.id);
        for (const DB::Track &track : tracks) {
            for (const DB::Clip &c : m_clipRepo.listByTrack(track.id)) {
                maxEnd = std::max(maxEnd, c.timelineStartUs + c.durationUs);
            }
        }
        if (maxEnd != m_sequence.durationUs) {
            m_sequence.durationUs = maxEnd;
            m_sequenceRepo.update(m_sequence);
        }

        rebuildComposition();
    }

    void EditorWindow::moveClip(const QString &clipId, qint64 newStartUs) {
        std::optional<DB::Clip> clip = m_clipRepo.findById(clipId);
        if (!clip) {
            return;
        }
        if (newStartUs < 0) newStartUs = 0;
        clip->timelineStartUs = newStartUs;
        m_clipRepo.update(*clip);

        // 扩展序列总时长（拖到更靠后时）。
        const qint64 clipEnd = newStartUs + clip->durationUs;
        if (clipEnd > m_sequence.durationUs) {
            m_sequence.durationUs = clipEnd;
            m_sequenceRepo.update(m_sequence);
        }

        rebuildComposition();
        seekTo(newStartUs);
    }

    void EditorWindow::exportProject() {
        if (m_exporting) {
            QMessageBox::information(this, QStringLiteral("导出"),
                                     QStringLiteral("已有导出任务进行中，请稍候。"));
            return;
        }
        if (m_sequence.id.isEmpty() || m_sequence.durationUs <= 0) {
            QMessageBox::information(this, QStringLiteral("导出"),
                                     QStringLiteral("时间线为空，先添加素材到时间线再导出。"));
            return;
        }

        // 选择输出路径（默认工程名.mp4）。
        const QString suggested =
            (m_project.title.isEmpty() ? QStringLiteral("export") : m_project.title) +
            QStringLiteral(".mp4");
        QString outPath = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出视频"), suggested,
            QStringLiteral("MP4 视频 (*.mp4)"));
        if (outPath.isEmpty()) {
            return;
        }
        if (!outPath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) {
            outPath += QStringLiteral(".mp4");
        }

        // 在主线程快照时间线（worker 不访问数据库）。
        Player::Exporter::Request req;
        req.outputPath = outPath;
        req.width  = m_project.width  > 0 ? m_project.width  : 1920;
        req.height = m_project.height > 0 ? m_project.height : 1080;
        req.fps    = m_project.fps    > 0 ? static_cast<int>(std::lround(m_project.fps)) : 30;
        req.durationUs = m_sequence.durationUs;

        const std::vector<DB::Track> tracks = m_trackRepo.listBySequence(m_sequence.id);
        req.tracks = tracks;
        QStringList missingFiles;   // 收集路径非空但文件不存在的素材
        for (const DB::Track &track : tracks) {
            std::vector<DB::Clip> clips = m_clipRepo.listByTrack(track.id);
            for (const DB::Clip &clip : clips) {
                const QString path = assetPathForClip(clip);
                if (!path.isEmpty()) {
                    if (!QFileInfo::exists(path)) {
                        // 素材文件已被移动/删除：导出会渲染成黑帧/静音，提前拦下。
                        if (!missingFiles.contains(path)) {
                            missingFiles.append(path);
                        }
                        continue;
                    }
                    req.assetPathByClip.insert(clip.id, path);
                }
            }
            req.clipsByTrack.insert(track.id, clips);
        }

        // 有素材文件缺失：直接中止并列出，避免跑完产出黑屏/静音的成片。
        if (!missingFiles.isEmpty()) {
            const QString list = missingFiles.join(QStringLiteral("\n"));
            QMessageBox::warning(
                this, QStringLiteral("导出"),
                QStringLiteral("以下素材文件不存在，无法导出。\n"
                               "请重新导入素材，或将文件放回原路径：\n\n%1").arg(list));
            return;
        }

        // 进度对话框（模态，可取消）。
        auto *dlg = new QProgressDialog(QStringLiteral("正在导出合成视频…"),
                                        QStringLiteral("取消"), 0, 100, this);
        dlg->setWindowModality(Qt::WindowModal);
        dlg->setMinimumDuration(0);
        dlg->setAutoClose(false);
        dlg->setAutoReset(false);
        dlg->setValue(0);

        // 起线程 + worker。
        m_exporting = true;
        m_exportThread = new QThread(this);
        m_exporter = new Player::Exporter;          // 无父对象，moveToThread
        m_exporter->setRequest(req);
        m_exporter->moveToThread(m_exportThread);

        connect(m_exportThread, &QThread::started, m_exporter, &Player::Exporter::run);
        connect(m_exporter, &Player::Exporter::progress, dlg, &QProgressDialog::setValue);
        connect(dlg, &QProgressDialog::canceled, m_exporter, &Player::Exporter::cancel,
                Qt::DirectConnection);
        connect(m_exporter, &Player::Exporter::finished, this,
                [this, dlg](bool ok, const QString &message) {
                    dlg->close();
                    dlg->deleteLater();
                    // 停线程并回收 worker。
                    if (m_exportThread) {
                        m_exportThread->quit();
                        m_exportThread->wait();
                        delete m_exportThread;
                        m_exportThread = nullptr;
                    }
                    delete m_exporter;
                    m_exporter = nullptr;
                    m_exporting = false;

                    if (ok) {
                        QMessageBox::information(
                            this, QStringLiteral("导出完成"),
                            QStringLiteral("已导出到：\n%1").arg(message));
                    } else {
                        QMessageBox::warning(this, QStringLiteral("导出"), message);
                    }
                }, Qt::QueuedConnection);

        m_exportThread->start();
    }

    QString EditorWindow::assetPathForClip(const DB::Clip &clip) {
        if (clip.assetId.isEmpty()) {
            return QString();
        }
        const std::optional<DB::MediaAsset> asset = m_assetRepo.findById(clip.assetId);
        if (!asset) {
            return QString();
        }
        // 本阶段只处理本地文件源；远程/云端源在后续阶段支持。
        return asset->filePath;
    }

    void EditorWindow::seekTo(qint64 timelineUs) {
        m_playheadUs = std::max<qint64>(0, timelineUs);
        if (m_timecodeLabel) {
            m_timecodeLabel->setText(formatTimecode(m_playheadUs));
        }
        if (m_timeline && m_timeline->playheadUs() != m_playheadUs) {
            m_timeline->setPlayheadUs(m_playheadUs);
        }
        // 横向滚动时间线，使播放头始终落在可视区内（留 80px 余量）。
        // 否则片段堆叠到很靠后时，seek/切换后播放头跑出视口，看上去"没反应"。
        if (m_timeline && m_timelineScroll) {
            m_timelineScroll->ensureVisible(m_timeline->playheadX(), 0, 80, 0);
        }
        if (m_engine && m_preview && !m_sequence.id.isEmpty()) {
            Player::VideoFrame frame = m_engine->composeAt(m_playheadUs);
            if (frame.valid()) {
                m_preview->setFrame(frame);
            }
        }
    }

    void EditorWindow::stepFrame(int direction) {
        // 按工程帧率换算单帧时长（缺省 30fps）。
        const double fps = m_project.fps > 0.0 ? m_project.fps : 30.0;
        const qint64 frameUs = static_cast<qint64>(1'000'000.0 / fps);
        seekTo(m_playheadUs + direction * frameUs);
    }

    QString EditorWindow::formatTimecode(qint64 us) {
        const qint64 totalMs = us / 1000;
        const qint64 min = totalMs / 60000;
        const qint64 sec = (totalMs / 1000) % 60;
        const qint64 ms = totalMs % 1000;
        return QStringLiteral("%1:%2.%3")
            .arg(min, 2, 10, QChar('0'))
            .arg(sec, 2, 10, QChar('0'))
            .arg(ms, 3, 10, QChar('0'));
    }

    void EditorWindow::closeEvent(QCloseEvent *event) {
        if (!m_closedEmitted) {
            m_closedEmitted = true;
            emit closed();
        }
        QMainWindow::closeEvent(event);
    }

    EditorWindow::~EditorWindow() {
        // 停止缩略图解码线程并回收 worker。
        // 先把代际推到一个不可能匹配的值，令正在解码的片段尽快中止本轮循环。
        if (m_thumbWorker) {
            m_thumbWorker->setGeneration(m_thumbGeneration + 1000);
        }
        if (m_thumbThread) {
            m_thumbThread->quit();
            m_thumbThread->wait();
        }
        // 线程已停，worker 不再运行其槽，主线程直接析构安全。
        delete m_thumbWorker;
        m_thumbWorker = nullptr;

        // 若仍有导出在进行：请求取消并等线程结束，避免悬空。
        if (m_exporter) {
            m_exporter->cancel();
        }
        if (m_exportThread) {
            m_exportThread->quit();
            m_exportThread->wait();
            delete m_exportThread;
            m_exportThread = nullptr;
        }
        delete m_exporter;
        m_exporter = nullptr;
    }

} // namespace Mixed
