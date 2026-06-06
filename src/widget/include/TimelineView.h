//
// Created by Anlk on 2026/6/6.
// TimelineView：自绘的可交互时间线控件。
// 自上而下绘制时间刻度 + 多条轨道行（每行内按 timeline_start_us/duration_us 定位片段块）
// + 可拖动播放头。支持：点选片段（高亮）、拖动播放头 seek、滚轮缩放（像素/秒）。
// 仅负责展示与交互，不直接操作数据库；通过信号把意图上报给 EditorWindow。
//

#ifndef MIXEDEDITING_TIMELINEVIEW_H
#define MIXEDEDITING_TIMELINEVIEW_H

#include <QHash>
#include <QImage>
#include <QVector>
#include <QWidget>
#include <vector>

#include "../../db/include/DbModels.h"

namespace Mixed {

    class TimelineView : public QWidget {
        Q_OBJECT

    public:
        explicit TimelineView(QWidget *parent = nullptr);

        // 设置轨道与各轨片段快照（轨道按 track_index 升序）。
        void setContent(const std::vector<DB::Track> &tracks,
                        const std::vector<std::vector<DB::Clip>> &clipsPerTrack,
                        qint64 sequenceDurationUs);

        // 设置片段缩略图。每个 clip.id 对应一组沿片段时长等间隔采样的关键帧
        // （从源起点到源终点），片段块按位置依次平铺这些帧 → 胶片条随时间变化。
        // 缺图时回退为纯色块。
        void setClipThumbnails(const QHash<QString, QVector<QImage>> &thumbs);

        // 增量更新单个片段的缩略图（工作线程逐片段解码完成时调用），仅重绘该片段块。
        void setClipThumbnail(const QString &clipId, const QVector<QImage> &frames);

        // 清空全部缩略图（重建/切换工程时调用），回退为纯色块直到新帧到达。
        void clearClipThumbnails();

        // 增量更新单个音频片段的波形（峰值包络，0~1），仅重绘该片段块。
        void setClipWaveform(const QString &clipId, const QVector<float> &peaks);
        // 清空全部波形（重建/切换工程时调用）。
        void clearClipWaveforms();

        // 当前播放头位置（微秒）。
        qint64 playheadUs() const { return m_playheadUs; }
        void setPlayheadUs(qint64 us);

        QString selectedClipId() const { return m_selectedClipId; }

        // 播放头在控件坐标系中的 x 像素（供外层滚动区跟随定位）。
        int playheadX() const { return usToX(m_playheadUs); }

    signals:
        void playheadMoved(qint64 us);
        void clipSelected(const QString &clipId);
        // 片段被拖动到新的时间线起点（微秒，已做不为负的钳制）。
        void clipMoved(const QString &clipId, qint64 newStartUs);
        // 右键菜单请求删除该片段（由 EditorWindow 弹确认框后再落库删除）。
        void clipDeleteRequested(const QString &clipId);

    protected:
        void paintEvent(QPaintEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;
        void contextMenuEvent(QContextMenuEvent *event) override;
        void wheelEvent(QWheelEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;
        QSize sizeHint() const override;

    private:
        // 像素 ↔ 时间换算。
        int    usToX(qint64 us) const;
        qint64 xToUs(int x) const;
        int    contentHeight() const;
        int    trackRowY(int trackIdx) const;
        // 命中测试：返回 (轨道下标, 片段下标)，未命中返回 {-1,-1}。
        std::pair<int, int> clipAt(const QPoint &pos) const;
        // 沿片段块平铺一组关键帧（每格取对应时间位置的帧），形成随时间变化的胶片条。
        void   paintFilmstrip(QPainter &p, const QRect &blockRect,
                              const QVector<QImage> &frames) const;
        // 在 audio 片段块上绘制波形条（peaks 为 0~1 峰值，按桶映射到块宽）。
        void   paintWaveform(QPainter &p, const QRect &blockRect,
                             const QVector<float> &peaks, bool selected) const;

        // fit 模式：按视口宽度自动算 pxPerSec，让整条序列完整可见。
        // 用户 Ctrl+滚轮缩放后退出 fit，转为手动缩放。
        void   applyFitIfNeeded();
        // 可用于绘制内容的视口宽度（时间区，扣除左侧轨道名列）。
        int    availableTimelineWidth() const;

        // 依据当前内容/缩放刷新最小尺寸，让外层滚动区扩展滚动范围。
        void   updateScrollExtent();

        std::vector<DB::Track> m_tracks;
        std::vector<std::vector<DB::Clip>> m_clipsPerTrack;
        qint64 m_sequenceDurationUs = 0;
        qint64 m_playheadUs = 0;
        QString m_selectedClipId;

        QHash<QString, QVector<QImage>> m_clipThumbs;
        QHash<QString, QVector<float>>  m_clipWaveforms;  // clipId -> 峰值包络(0~1)

        bool m_draggingPlayhead = false;

        // 片段拖动状态。
        bool    m_draggingClip = false;
        int     m_dragTrackIdx = -1;     // 被拖片段所在轨道下标
        int     m_dragClipIdx = -1;      // 被拖片段在该轨的下标
        qint64  m_dragOrigStartUs = 0;   // 拖动前的原始起点
        int     m_dragGrabDx = 0;        // 鼠标按下点相对片段左缘的像素偏移
        bool    m_dragMoved = false;     // 是否已发生有效位移（区分点选与拖动）

        double m_pxPerSec = 60.0;   // 缩放：每秒像素数
        bool   m_fitMode = true;    // true：按视口宽度自动适配；Ctrl+滚轮后转 false 手动缩放

        // 布局常量
        static constexpr int kRulerH = 24;      // 顶部刻度高度
        static constexpr int kTrackH = 56;      // 每条轨道行高
        static constexpr int kTrackGap = 6;     // 轨道间距
        static constexpr int kLabelW = 76;      // 左侧轨道名宽度
        static constexpr int kDragThresholdPx = 4; // 超过此位移才视为拖动
    };

} // namespace Mixed

#endif // MIXEDEDITING_TIMELINEVIEW_H
