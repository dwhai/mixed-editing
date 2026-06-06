//
// Created by Anlk on 2026/6/6.
// TimelineView 实现。
//

#include "../include/TimelineView.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace Mixed {

    TimelineView::TimelineView(QWidget *parent) : QWidget(parent) {
        setObjectName("timelineView");
        setMouseTracking(true);
        setMinimumHeight(kRulerH + kTrackH + kTrackGap);
    }

    void TimelineView::setContent(const std::vector<DB::Track> &tracks,
                                  const std::vector<std::vector<DB::Clip>> &clipsPerTrack,
                                  qint64 sequenceDurationUs) {
        m_tracks = tracks;
        m_clipsPerTrack = clipsPerTrack;
        m_sequenceDurationUs = sequenceDurationUs;

        applyFitIfNeeded();   // 内容变化后按视口重算适配缩放（fit 模式下）
        updateScrollExtent();
        update();
    }

    void TimelineView::setClipThumbnails(const QHash<QString, QVector<QImage>> &thumbs) {
        m_clipThumbs = thumbs;
        update();
    }

    void TimelineView::setClipThumbnail(const QString &clipId,
                                        const QVector<QImage> &frames) {
        m_clipThumbs.insert(clipId, frames);
        // 只重绘该片段所在区域，避免整条时间线重绘。
        for (size_t i = 0; i < m_clipsPerTrack.size(); ++i) {
            const int rowY = trackRowY(static_cast<int>(i));
            for (const DB::Clip &clip : m_clipsPerTrack[i]) {
                if (clip.id == clipId) {
                    const int x = usToX(clip.timelineStartUs);
                    const int cw = std::max(
                        4, static_cast<int>(clip.durationUs / 1'000'000.0 * m_pxPerSec));
                    update(QRect(x - 2, rowY, cw + 4, kTrackH));
                    return;
                }
            }
        }
        update();
    }

    void TimelineView::clearClipThumbnails() {
        if (!m_clipThumbs.isEmpty()) {
            m_clipThumbs.clear();
            update();
        }
    }

    void TimelineView::setClipWaveform(const QString &clipId,
                                       const QVector<float> &peaks) {
        m_clipWaveforms.insert(clipId, peaks);
        for (size_t i = 0; i < m_clipsPerTrack.size(); ++i) {
            const int rowY = trackRowY(static_cast<int>(i));
            for (const DB::Clip &clip : m_clipsPerTrack[i]) {
                if (clip.id == clipId) {
                    const int x = usToX(clip.timelineStartUs);
                    const int cw = std::max(
                        4, static_cast<int>(clip.durationUs / 1'000'000.0 * m_pxPerSec));
                    update(QRect(x - 2, rowY, cw + 4, kTrackH));
                    return;
                }
            }
        }
        update();
    }

    void TimelineView::clearClipWaveforms() {
        if (!m_clipWaveforms.isEmpty()) {
            m_clipWaveforms.clear();
            update();
        }
    }

    int TimelineView::availableTimelineWidth() const {
        // 优先用外层视口宽度（滚动区 viewport）；取不到则用自身宽度。
        int vw = width();
        if (QWidget *vp = parentWidget()) {
            vw = vp->width();
        }
        return std::max(50, vw - kLabelW - 8);
    }

    void TimelineView::applyFitIfNeeded() {
        if (!m_fitMode) {
            return;
        }
        // 让整条序列（至少 10 秒占位）铺满可用视口宽度。
        const qint64 spanUs = std::max<qint64>(m_sequenceDurationUs, 10'000'000);
        const double spanSec = spanUs / 1'000'000.0;
        if (spanSec <= 0.0) {
            return;
        }
        const double fit = availableTimelineWidth() / spanSec;
        // fit 模式下不设下限（长视频要能缩很小），仅设上限避免短素材过度放大。
        m_pxPerSec = std::clamp(fit, 0.05, 200.0);
    }

    int TimelineView::trackRowY(int trackIdx) const {
        return kRulerH + kTrackGap + trackIdx * (kTrackH + kTrackGap);
    }

    std::pair<int, int> TimelineView::clipAt(const QPoint &pos) const {
        for (size_t i = 0; i < m_tracks.size(); ++i) {
            const int rowY = trackRowY(static_cast<int>(i));
            if (pos.y() < rowY || pos.y() >= rowY + kTrackH) {
                continue;
            }
            if (i >= m_clipsPerTrack.size()) break;
            const auto &clips = m_clipsPerTrack[i];
            for (size_t j = 0; j < clips.size(); ++j) {
                const int x = usToX(clips[j].timelineStartUs);
                const int cw = std::max(
                    4, static_cast<int>(clips[j].durationUs / 1'000'000.0 * m_pxPerSec));
                if (pos.x() >= x && pos.x() < x + cw) {
                    return {static_cast<int>(i), static_cast<int>(j)};
                }
            }
        }
        return {-1, -1};
    }

    void TimelineView::paintFilmstrip(QPainter &p, const QRect &blockRect,
                                      const QVector<QImage> &frames) const {
        if (frames.isEmpty() || blockRect.width() <= 0 || blockRect.height() <= 0) {
            return;
        }
        // 约定：frames 已由上层预缩放到 kTrackH-8 量级（见 EditorWindow），
        // 这里只做平铺 blit，绝不在重绘热路径里 scale，避免播放头拖动时卡顿。
        const QImage &first = frames.front();
        if (first.isNull() || first.height() <= 0) return;
        const int tileW = std::max(1, first.width());

        const int span = blockRect.width();
        const int n = static_cast<int>(frames.size());
        const int top = blockRect.top();
        const int h = blockRect.height();

        for (int x = blockRect.left(); x < blockRect.right(); x += tileW) {
            const double frac = span > 0
                ? std::clamp(static_cast<double>(x - blockRect.left()) / span, 0.0, 0.999)
                : 0.0;
            int idx = static_cast<int>(frac * n);
            idx = std::clamp(idx, 0, n - 1);

            const QImage &tile = frames[idx];
            if (tile.isNull()) continue;

            const int drawW = std::min(tileW, blockRect.right() - x);
            // 源宽不足 drawW 时按可用宽裁切；高度按块高拉伸（预缩放已接近，开销极小）。
            p.drawImage(QRect(x, top, drawW, h), tile,
                        QRect(0, 0, std::min(drawW, tile.width()), tile.height()));
            p.setPen(QPen(QColor(0, 0, 0, 60), 1));
            p.drawLine(x, top, x, blockRect.bottom());
        }
        p.fillRect(blockRect, QColor(0, 0, 0, 18));
    }

    void TimelineView::paintWaveform(QPainter &p, const QRect &blockRect,
                                     const QVector<float> &peaks, bool selected) const {
        if (peaks.isEmpty() || blockRect.width() <= 0 || blockRect.height() <= 0) {
            return;
        }
        const int n = peaks.size();
        const int left = blockRect.left();
        const int w = blockRect.width();
        const int midY = blockRect.center().y();
        const int halfH = std::max(1, blockRect.height() / 2 - 2);

        // 每个像素列取对应桶的峰值，画一条对称竖线（中心向上下展开）。
        p.setPen(QPen(QColor(selected ? "#eafff6" : "#d6fff0"), 1));
        for (int x = 0; x < w; ++x) {
            const double frac = static_cast<double>(x) / w;
            int idx = static_cast<int>(frac * n);
            idx = std::clamp(idx, 0, n - 1);
            const float peak = std::clamp(peaks[idx], 0.0f, 1.0f);
            const int barH = static_cast<int>(peak * halfH);
            if (barH <= 0) continue;
            const int px = left + x;
            p.drawLine(px, midY - barH, px, midY + barH);
        }
        // 中线。
        p.setPen(QPen(QColor(0, 0, 0, 40), 1));
        p.drawLine(left, midY, left + w, midY);
    }

    void TimelineView::updateScrollExtent() {
        const QSize hint = sizeHint();
        setMinimumWidth(hint.width());
        setMinimumHeight(hint.height());
        // 外层 QScrollArea 在 widgetResizable=true 时按本控件的最小尺寸决定滚动范围，
        // 故内容/缩放变化后必须刷新最小尺寸，否则无法滚到后段或缩放后越界。
        resize(hint.width(), std::max(hint.height(), height()));
        updateGeometry();
    }

    void TimelineView::setPlayheadUs(qint64 us) {
        us = std::max<qint64>(0, us);
        if (us != m_playheadUs) {
            m_playheadUs = us;
            update();
        }
    }

    int TimelineView::usToX(qint64 us) const {
        return kLabelW + static_cast<int>(us / 1'000'000.0 * m_pxPerSec);
    }

    qint64 TimelineView::xToUs(int x) const {
        const double sec = std::max(0, x - kLabelW) / m_pxPerSec;
        return static_cast<qint64>(sec * 1'000'000.0);
    }

    int TimelineView::contentHeight() const {
        const int n = static_cast<int>(m_tracks.size());
        return kRulerH + n * (kTrackH + kTrackGap) + kTrackGap;
    }

    QSize TimelineView::sizeHint() const {
        // 宽度按序列时长 + 余量；高度按轨道数。
        const qint64 spanUs = std::max<qint64>(m_sequenceDurationUs, 10'000'000);
        const int w = usToX(spanUs) + 200;
        return QSize(w, std::max(contentHeight(), kRulerH + kTrackH + kTrackGap));
    }

    void TimelineView::paintEvent(QPaintEvent *event) {
        QPainter p(this);
        p.fillRect(rect(), QColor("#ffffff"));

        const int w = width();
        // 仅绘制本次曝光的可视范围，避免在数十万像素宽的内容上整条遍历。
        const QRect dirty = event->rect();
        const int x0 = std::max(kLabelW, dirty.left());
        const int x1 = std::min(w, dirty.right() + 1);

        // ---- 顶部刻度 ----
        p.fillRect(0, 0, w, kRulerH, QColor("#f4f5f7"));
        p.setPen(QColor("#8b92a3"));
        QFont f = p.font();
        f.setPixelSize(10);
        p.setFont(f);
        // 自适应刻度：标签间距随缩放从 nice step 序列里挑，保证相邻标签 ≥60px，
        // 避免 fit 模式（px/sec 很小）时每秒一格导致刻度爆炸。
        const double pxPerSec = m_pxPerSec;
        if (pxPerSec > 0.0) {
            static const int kSteps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300,
                                         600, 900, 1800, 3600, 7200, 18000, 36000};
            int stepSec = kSteps[0];
            for (int s : kSteps) {
                stepSec = s;
                if (s * pxPerSec >= 60.0) break; // 标签间距达标即止
            }
            // 起始秒对齐到 stepSec 的整数倍。
            int startSec = static_cast<int>((x0 - kLabelW) / pxPerSec);
            startSec -= startSec % stepSec;
            if (startSec < 0) startSec = 0;
            for (int sec = startSec;; sec += stepSec) {
                const int x = kLabelW + static_cast<int>(sec * pxPerSec);
                if (x >= x1) break;
                p.setPen(QColor("#b6bcc8"));
                p.drawLine(x, 6, x, kRulerH);
                p.setPen(QColor("#8b92a3"));
                p.drawText(x + 2, 12,
                           QStringLiteral("%1:%2")
                               .arg(sec / 60, 2, 10, QChar('0'))
                               .arg(sec % 60, 2, 10, QChar('0')));
            }
        }

        // ---- 轨道与片段 ----
        for (size_t i = 0; i < m_tracks.size(); ++i) {
            const DB::Track &track = m_tracks[i];
            const int rowY = kRulerH + kTrackGap +
                             static_cast<int>(i) * (kTrackH + kTrackGap);

            // 轨道名区 + 轨道底。
            p.fillRect(0, rowY, kLabelW, kTrackH, QColor("#f9fafb"));
            p.fillRect(kLabelW, rowY, w - kLabelW, kTrackH, QColor("#eef0f4"));
            p.setPen(QColor("#5a6172"));
            p.drawText(QRect(6, rowY, kLabelW - 10, kTrackH),
                       Qt::AlignVCenter | Qt::AlignLeft, track.name);

            // 片段块。
            if (i < m_clipsPerTrack.size()) {
                for (const DB::Clip &clip : m_clipsPerTrack[i]) {
                    const int x = usToX(clip.timelineStartUs);
                    const int cw = std::max(
                        4, static_cast<int>(clip.durationUs / 1'000'000.0 * pxPerSec));
                    // 完全落在可视范围外的片段跳过（含选中描边的少量余量）。
                    if (x + cw < x0 - 2 || x > x1 + 2) {
                        continue;
                    }
                    const QRect blockRect(x, rowY + 4, cw, kTrackH - 8);

                    const bool selected = (clip.id == m_selectedClipId);

                    // 圆角裁剪范围，作为胶片条/底色的绘制边界。
                    QPainterPath clipPath;
                    clipPath.addRoundedRect(blockRect, 6, 6);
                    p.save();
                    p.setClipPath(clipPath);

                    const QVector<QImage> frames = m_clipThumbs.value(clip.id);
                    const bool isAudio = (clip.kind == QStringLiteral("audio"));
                    if (isAudio) {
                        // 音频片段：底色 + 波形条（波形未就绪时仅底色）。
                        p.fillRect(blockRect, QColor(selected ? "#2aa775" : "#1f9d6b"));
                        const QVector<float> peaks = m_clipWaveforms.value(clip.id);
                        if (!peaks.isEmpty()) {
                            paintWaveform(p, blockRect, peaks, selected);
                        }
                    } else if (!frames.isEmpty()) {
                        // 视频/图片片段：沿时长平铺采样帧，胶片条随时间变化。
                        paintFilmstrip(p, blockRect, frames);
                    } else {
                        // 无缩略图（解码中/失败）：回退纯色块。
                        p.fillRect(blockRect, QColor(selected ? "#4b82f8" : "#2f6df6"));
                    }
                    p.restore();

                    // 选中描边（画在裁剪外，保证完整可见）。
                    p.setBrush(Qt::NoBrush);
                    p.setPen(selected ? QPen(QColor("#1a3f9e"), 2)
                                      : QPen(QColor("#1f4fd0"), 1));
                    p.drawRoundedRect(blockRect, 6, 6);
                }
            }
        }

        // ---- 播放头 ----
        const int phx = usToX(m_playheadUs);
        p.setPen(QPen(QColor("#ff4d4f"), 2));
        p.drawLine(phx, 0, phx, height());
        // 播放头顶部把手。
        p.setBrush(QColor("#ff4d4f"));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygon({QPoint(phx - 5, 0), QPoint(phx + 5, 0), QPoint(phx, 8)}));
    }

    void TimelineView::mousePressEvent(QMouseEvent *event) {
        const QPoint pos = event->pos();

        // 仅左键参与拖动/seek；右键交给 contextMenuEvent 弹菜单，避免抢占拖动状态。
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }

        // 点在刻度区：开始拖动播放头。
        if (pos.y() < kRulerH) {
            m_draggingPlayhead = true;
            setPlayheadUs(xToUs(pos.x()));
            emit playheadMoved(m_playheadUs);
            return;
        }

        // 命中片段：选中并进入"待拖动"状态（位移超过阈值才真正移动）。
        const auto [ti, ci] = clipAt(pos);
        if (ti >= 0 && ci >= 0) {
            const DB::Clip &clip = m_clipsPerTrack[ti][ci];
            m_selectedClipId = clip.id;
            emit clipSelected(clip.id);

            m_draggingClip = true;
            m_dragTrackIdx = ti;
            m_dragClipIdx = ci;
            m_dragOrigStartUs = clip.timelineStartUs;
            m_dragGrabDx = pos.x() - usToX(clip.timelineStartUs);
            m_dragMoved = false;

            setCursor(Qt::ClosedHandCursor);
            update();
            return;
        }

        // 空白区点击：把播放头移到该处。
        m_draggingPlayhead = true;
        setPlayheadUs(xToUs(pos.x()));
        emit playheadMoved(m_playheadUs);
    }

    void TimelineView::mouseMoveEvent(QMouseEvent *event) {
        const QPoint pos = event->pos();

        if (m_draggingPlayhead) {
            setPlayheadUs(xToUs(pos.x()));
            emit playheadMoved(m_playheadUs);
            return;
        }

        if (m_draggingClip && m_dragTrackIdx >= 0 &&
            m_dragTrackIdx < static_cast<int>(m_clipsPerTrack.size()) &&
            m_dragClipIdx < static_cast<int>(m_clipsPerTrack[m_dragTrackIdx].size())) {
            // 新起点 = 鼠标 x 扣除抓取偏移，换算回时间并钳制不为负。
            const int newLeftX = pos.x() - m_dragGrabDx;
            qint64 newStartUs = xToUs(newLeftX);
            if (newStartUs < 0) newStartUs = 0;

            // 位移超过阈值才视为"拖动"，否则当作纯点选（释放时不落库）。
            if (std::abs(usToX(newStartUs) - usToX(m_dragOrigStartUs)) >= kDragThresholdPx) {
                m_dragMoved = true;
            }
            // 实时更新本地快照，立即重绘（释放时才落库）。
            m_clipsPerTrack[m_dragTrackIdx][m_dragClipIdx].timelineStartUs = newStartUs;
            update();
            return;
        }

        // 悬停在片段上时给出可拖动光标提示。
        const auto [ti, ci] = clipAt(pos);
        setCursor((ti >= 0 && ci >= 0) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }

    void TimelineView::mouseReleaseEvent(QMouseEvent *event) {
        Q_UNUSED(event);
        m_draggingPlayhead = false;

        if (m_draggingClip) {
            const bool moved = m_dragMoved && m_dragTrackIdx >= 0 &&
                m_dragTrackIdx < static_cast<int>(m_clipsPerTrack.size()) &&
                m_dragClipIdx < static_cast<int>(m_clipsPerTrack[m_dragTrackIdx].size());
            if (moved) {
                const DB::Clip &clip = m_clipsPerTrack[m_dragTrackIdx][m_dragClipIdx];
                if (clip.timelineStartUs != m_dragOrigStartUs) {
                    emit clipMoved(clip.id, clip.timelineStartUs);
                }
            }
            m_draggingClip = false;
            m_dragTrackIdx = m_dragClipIdx = -1;
            m_dragMoved = false;
            setCursor(Qt::OpenHandCursor);
        }
    }

    void TimelineView::contextMenuEvent(QContextMenuEvent *event) {
        // 命中片段才弹菜单：选中它，提供"从时间线删除"。
        const auto [ti, ci] = clipAt(event->pos());
        if (ti < 0 || ci < 0) {
            event->ignore();
            return;
        }
        const QString clipId = m_clipsPerTrack[ti][ci].id;
        m_selectedClipId = clipId;
        update();

        // 用 popup()（非阻塞）而非 exec()：exec() 会在 contextMenuEvent 内开启
        // 嵌套事件循环，在 macOS 上叠加滚动区/视口时易触发窗口几何空指针崩溃。
        // 菜单设置 DeleteOnClose 自管生命周期，动作通过信号异步触发。
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        QAction *delAct = menu->addAction(QStringLiteral("从时间线删除片段"));
        connect(delAct, &QAction::triggered, this, [this, clipId]() {
            // 实际删除由 EditorWindow 弹确认框后执行。
            emit clipDeleteRequested(clipId);
        });
        menu->popup(event->globalPos());
        event->accept();
    }

    void TimelineView::wheelEvent(QWheelEvent *event) {
        // Ctrl + 滚轮缩放时间轴；普通滚轮交给父级滚动区。
        if (event->modifiers() & Qt::ControlModifier) {
            // 首次缩放即退出 fit 模式，转为手动缩放（保留用户设定的 pxPerSec）。
            m_fitMode = false;
            const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
            m_pxPerSec = std::clamp(m_pxPerSec * factor, 1.0, 400.0);
            updateScrollExtent();
            update();
            event->accept();
        } else {
            QWidget::wheelEvent(event);
        }
    }

    void TimelineView::resizeEvent(QResizeEvent *event) {
        QWidget::resizeEvent(event);
        // 视口尺寸变化时，fit 模式需重算缩放使整条序列仍完整可见。
        if (m_fitMode) {
            const double before = m_pxPerSec;
            applyFitIfNeeded();
            if (!qFuzzyCompare(before, m_pxPerSec)) {
                updateScrollExtent();
                update();
            }
        }
    }

} // namespace Mixed
