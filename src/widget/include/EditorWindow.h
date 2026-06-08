//
// Created by Anlk on 2026/6/6.
// 视频剪辑窗口：独立的全屏工作区，从主窗口「开始创作」进入。
// 打开时创建一个工程 + 主序列 + 默认视频/音频轨道（落库）；导入素材写入 media_asset
// 并刷新素材库；双击素材在视频轨末尾追加片段（clip）并刷新时间线。
// 布局结构在此构建，视觉样式统一由 Theme 提供。
// 关闭时发出 closed() 信号，由 MainWindow 据此恢复主窗口显示。
//

#ifndef MIXEDEDITING_EDITORWINDOW_H
#define MIXEDEDITING_EDITORWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QHash>
#include <QImage>
#include <QVector>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include "../../db/include/DbModels.h"
#include "../../db/include/Repositories.h"

class QLabel;
class QVBoxLayout;
class QScrollArea;
class QListWidget;
class QThread;
class QPushButton;

namespace Mixed {

    namespace Player {
        class VideoGLWidget;
        class CompositionEngine;
        class ClipSource;
        class Exporter;
        class PlaybackController;
    }

    class TimelineView;
    class ExportQueueManager;
    class ExportQueuePanel;

    // 缩略图解码工作体：在独立线程上用 FFmpeg/ClipSource 解码片段采样帧，
    // 避免阻塞主线程。请求带"代际号"，新一轮重建会令旧代际请求被丢弃。
    // 解码完成通过 clipReady 信号（队列连接）回主线程逐片段刷新。
    class ThumbnailWorker : public QObject {
        Q_OBJECT
    public:
        explicit ThumbnailWorker(QObject *parent = nullptr);
        ~ThumbnailWorker() override;

        // 当前有效代际：主线程在每轮重建前自增并 setGeneration，
        // worker 解码每个片段前比对，过期则跳过（原子量，跨线程读写安全）。
        void setGeneration(quint64 gen) { m_generation.store(gen); }

    public slots:
        // 解码单个片段的采样帧（在 worker 线程执行）。reqGen 为发起时的代际号。
        void decodeClip(quint64 reqGen, const QString &clipId, const QString &filePath,
                        qint64 sourceInUs, qint64 sourceOutUs, int tileH);
        // 解码单个音频片段的波形峰值包络（在 worker 线程执行）。buckets 为目标桶数。
        void decodeWaveform(quint64 reqGen, const QString &clipId, const QString &filePath,
                            qint64 sourceInUs, qint64 sourceOutUs, int buckets);

    signals:
        // 某片段解码完成（仅当 reqGen 仍等于当前代际才由主线程采纳）。
        void clipReady(quint64 reqGen, const QString &clipId, const QVector<QImage> &frames);
        // 某音频片段波形解码完成（峰值 0~1）。
        void waveformReady(quint64 reqGen, const QString &clipId, const QVector<float> &peaks);

    private:
        std::atomic<quint64> m_generation{0};
        // 同文件解码器缓存（worker 线程内使用），按代际清理。
        std::unordered_map<std::string, std::unique_ptr<Player::ClipSource>> m_sources;
        quint64 m_cacheGen = 0;
    };

    class EditorWindow : public QMainWindow {
        Q_OBJECT

    public:
        // projectId 非空：打开该已有工程；为空：新建模式（首次导入/添加片段时落库建新工程）。
        explicit EditorWindow(const QString &projectId = QString(), QWidget *parent = nullptr);
        ~EditorWindow() override;

    signals:
        // 窗口关闭时发出，MainWindow 接此信号重新显示主界面。
        void closed();

    protected:
        void closeEvent(QCloseEvent *event) override;
        bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        void setupUi();
        QWidget *buildTopBar();      // 顶部：返回 + 工程名 + 时间码 + 逐帧 + 导出
        QWidget *buildMediaPanel();  // 左侧：素材库
        QWidget *buildPreviewArea(); // 中间：预览画面（合成输出）
        QWidget *buildTimeline();    // 底部：时间线轨道

        // 数据层
        void ensureProject();        // 惰性创建工程 + 主序列 + 默认轨道（首次导入/添加片段时触发）
        bool loadExistingProject();  // 仅加载已有工程（不创建）；成功返回 true。进入编辑器时调用。
        bool loadProject(const QString &projectId); // 按 id 精确加载指定工程；成功返回 true。
        void renameProject();        // 双击工程名 → 弹框重命名（新工程会先落库）
        void importMedia();          // 选文件 → 探测 → 落库 → 刷新
        void addClipFromAsset(const DB::MediaAsset &asset); // 追加片段到视频/音频轨
        void deleteAsset(const DB::MediaAsset &asset);      // 从素材库删除素材 + 级联删除其时间线片段
        void refreshMediaList();     // 重绘素材库
        void showMediaContextMenu(const DB::MediaAsset &asset, const QPoint &globalPos);

        // 合成预览
        void rebuildComposition();   // 从数据库重建合成引擎的时间线快照 + 刷新时间线控件
        void refreshClipThumbnails();// 为各片段解码代表帧，下发给时间线控件做胶片条
        void moveClip(const QString &clipId, qint64 newStartUs); // 拖动后持久化片段新起点
        void deleteClip(const QString &clipId);  // 确认后从时间线删除单个片段
        void rippleDeleteClip(const QString &clipId);             // 波纹删除：后续片段前移补位
        void trimClip(const QString &clipId, qint64 newStartUs,
                      qint64 newSourceInUs, qint64 newSourceOutUs,
                      qint64 newDurationUs);                       // 拖两端裁剪后落库
        void splitClipAt(const QString &clipId, qint64 atUs);     // 在播放头处分割片段
        void splitSelectedAtPlayhead();                           // 顶栏“分割”按钮入口
        void compactTracks();        // 紧凑排列：消除各轨片段间隙（多视频合并到一轨）
        void addSelectedAssetsToTimeline();   // 批量把素材库选中项按序追加到时间线
        void exportProject();        // 选画质/路径 → 入队后台导出（MP4）
        void seekTo(qint64 timelineUs);   // 移动播放头 → 合成该时刻 → 显示
        void applyPlayheadChrome(qint64 timelineUs); // 仅刷新播放头 UI（时间码/控件/滚动）
        void stepFrame(int direction);    // 逐帧步进（±1 帧）
        // 读取当前序列轨道/各轨片段/clip→源路径快照（导出与播放共用）。
        // missingFiles 非空时收集“路径非空但文件不存在”的素材路径。序列为空返回 false。
        bool buildTimelineSnapshot(std::vector<DB::Track> &tracks,
                                   QHash<QString, std::vector<DB::Clip>> &clipsByTrack,
                                   QHash<QString, QString> &assetPathByClip,
                                   QStringList *missingFiles);
        QString assetPathForClip(const DB::Clip &clip);
        static QString formatTimecode(qint64 us);

        // 仓库
        DB::ProjectRepository    m_projectRepo;
        DB::MediaAssetRepository m_assetRepo;
        DB::SequenceRepository   m_sequenceRepo;
        DB::TrackRepository      m_trackRepo;
        DB::ClipRepository       m_clipRepo;

        // 当前工程上下文
        DB::Project  m_project;
        QString      m_requestedProjectId; // 构造时请求打开的工程 id（空=新建模式）
        DB::Sequence m_sequence;
        QString      m_videoTrackId;
        QString      m_audioTrackId;
        qint64       m_playheadUs = 0;

        // 合成引擎与预览
        std::unique_ptr<Player::CompositionEngine> m_engine;
        Player::VideoGLWidget *m_preview = nullptr;
        TimelineView          *m_timeline = nullptr;
        QScrollArea           *m_timelineScroll = nullptr;

        // 实时播放控制器（自带独立合成引擎 + 音频输出，跑在 worker 线程）。
        Player::PlaybackController *m_playback = nullptr;
        QPushButton               *m_playButton = nullptr;

        // 动态填充的容器
        QListWidget *m_mediaList = nullptr;
        QLabel      *m_projectTitle = nullptr;
        QLabel      *m_timecodeLabel = nullptr;

        // 缩略图后台解码：worker 跑在 m_thumbThread 上，主线程只派发请求/收结果。
        QThread         *m_thumbThread = nullptr;
        ThumbnailWorker *m_thumbWorker = nullptr;
        quint64          m_thumbGeneration = 0; // 每轮重建自增，用于丢弃过期解码结果

        // 导出：交由队列管理器（非阻塞 + 多任务顺序执行），进度显示在 m_exportPanel。
        ExportQueueManager *m_exportQueue = nullptr;
        ExportQueuePanel   *m_exportPanel = nullptr;

        bool m_closedEmitted = false; // 防止 closeEvent 重复发信号

    signals:
        // 派发一个片段解码请求给 worker（队列连接，跨线程）。
        void requestThumbnail(quint64 reqGen, const QString &clipId, const QString &filePath,
                              qint64 sourceInUs, qint64 sourceOutUs, int tileH);
        // 派发一个音频片段波形解码请求给 worker。
        void requestWaveform(quint64 reqGen, const QString &clipId, const QString &filePath,
                             qint64 sourceInUs, qint64 sourceOutUs, int buckets);

    private slots:
        // worker 解码完成回调（主线程）：代际匹配才更新到时间线。
        void onThumbnailReady(quint64 reqGen, const QString &clipId,
                              const QVector<QImage> &frames);
        void onWaveformReady(quint64 reqGen, const QString &clipId,
                             const QVector<float> &peaks);

        // 实时播放回调（主线程）。
        void togglePlayback();                       // ▶/⏸ 按钮
        void onPlaybackPosition(qint64 timelineUs);  // 播放推进 → 刷新播放头 UI
        void onPlaybackEnded();                      // 到末尾自动停止
    };

} // namespace Mixed

#endif // MIXEDEDITING_EDITORWINDOW_H
