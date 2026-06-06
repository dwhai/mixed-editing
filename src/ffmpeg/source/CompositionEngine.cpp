//
// Created by Anlk on 2026/6/6.
// CompositionEngine 实现。
//

#include "../include/CompositionEngine.h"
#include "../include/ClipSource.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <algorithm>

extern "C" {
#include <libswscale/swscale.h>
}

namespace Mixed::Player {

    CompositionEngine::CompositionEngine() = default;
    CompositionEngine::~CompositionEngine() {
        if (m_yuvSws) {
            sws_freeContext(m_yuvSws);
            m_yuvSws = nullptr;
        }
    }

    void CompositionEngine::setCanvasSize(int width, int height) {
        if (width > 0 && height > 0) {
            m_canvasW = width;
            m_canvasH = height;
        }
    }

    void CompositionEngine::setTimeline(
        const std::vector<DB::Track> &tracks,
        const std::unordered_map<QString, std::vector<DB::Clip>> &clipsByTrack,
        const std::unordered_map<QString, QString> &assetPathByClip) {
        m_tracks = tracks;
        m_clipsByTrack = clipsByTrack;
        m_assetPathByClip = assetPathByClip;

        // 移除已不在时间线上的片段对应的 ClipSource，回收资源。
        for (auto it = m_sources.begin(); it != m_sources.end();) {
            if (m_assetPathByClip.find(it->first) == m_assetPathByClip.end()) {
                it = m_sources.erase(it);
            } else {
                ++it;
            }
        }
    }

    void CompositionEngine::clearSources() {
        m_sources.clear();
    }

    ClipSource *CompositionEngine::sourceFor(const QString &clipId, const QString &path) {
        auto it = m_sources.find(clipId);
        if (it != m_sources.end()) {
            return it->second.get();
        }
        auto src = std::make_unique<ClipSource>();
        if (!src->open(path)) {
            return nullptr;
        }
        ClipSource *raw = src.get();
        m_sources.emplace(clipId, std::move(src));
        return raw;
    }

    CompositionEngine::Placement CompositionEngine::placementFor(const DB::Clip &clip,
                                                                 const QImage &srcImage) const {
        Placement p;
        p.opacity = std::clamp(clip.opacity, 0.0, 1.0);

        // 默认：素材按「contain」方式缩放铺满画布并居中（保持源宽高比）。
        const double srcW = srcImage.width();
        const double srcH = srcImage.height();
        if (srcW <= 0 || srcH <= 0) {
            return p;
        }
        const double scale = std::min(m_canvasW / srcW, m_canvasH / srcH);
        double baseW = srcW * scale;
        double baseH = srcH * scale;
        double baseX = (m_canvasW - baseW) / 2.0;
        double baseY = (m_canvasH - baseH) / 2.0;

        // 叠加 clip.transform（JSON：x/y 为画布像素偏移，scale 为附加缩放）。
        double tx = 0.0, ty = 0.0, tScale = 1.0;
        if (!clip.transform.isEmpty()) {
            const QJsonObject o =
                QJsonDocument::fromJson(clip.transform.toUtf8()).object();
            tx = o.value(QStringLiteral("x")).toDouble(0.0);
            ty = o.value(QStringLiteral("y")).toDouble(0.0);
            tScale = o.value(QStringLiteral("scale")).toDouble(1.0);
            if (tScale <= 0.0) tScale = 1.0;
        }

        p.w = baseW * tScale;
        p.h = baseH * tScale;
        // 缩放围绕中心，再叠加平移。
        p.x = baseX - (p.w - baseW) / 2.0 + tx;
        p.y = baseY - (p.h - baseH) / 2.0 + ty;
        return p;
    }

    QImage CompositionEngine::applyEffects(const QString & /*clipId*/, const QImage &src) const {
        // 接口位：后续阶段在此对每个 effect 依 order_index 应用 libavfilter 单滤镜。
        return src;
    }

    VideoFrame CompositionEngine::composeAt(qint64 timelineUs) {
        // 画布：黑色不透明底。
        QImage canvas(m_canvasW, m_canvasH, QImage::Format_RGBA8888);
        canvas.fill(QColor(0, 0, 0, 255));

        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // 轨道按 track_index 升序叠加：小者在底层先画，大者覆盖其上。
        std::vector<DB::Track> ordered = m_tracks;
        std::sort(ordered.begin(), ordered.end(),
                  [](const DB::Track &a, const DB::Track &b) {
                      return a.trackIndex < b.trackIndex;
                  });

        for (const DB::Track &track : ordered) {
            // 本阶段只合成视频/图片轨；音频/文字轨跳过。
            if (track.trackType != QStringLiteral("video")) {
                continue;
            }
            if (track.isHidden) {
                continue;
            }
            auto clipsIt = m_clipsByTrack.find(track.id);
            if (clipsIt == m_clipsByTrack.end()) {
                continue;
            }

            for (const DB::Clip &clip : clipsIt->second) {
                if (!clip.isEnabled) continue;
                // 命中判定：timelineUs 落在片段时间线区间内。
                if (timelineUs < clip.timelineStartUs ||
                    timelineUs >= clip.timelineStartUs + clip.durationUs) {
                    continue;
                }

                auto pathIt = m_assetPathByClip.find(clip.id);
                if (pathIt == m_assetPathByClip.end() || pathIt->second.isEmpty()) {
                    continue;
                }
                ClipSource *src = sourceFor(clip.id, pathIt->second);
                if (!src || !src->isOpen()) {
                    continue;
                }

                // 时间线时刻 → 源时刻：考虑片段 trim 起点与变速。
                const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
                const qint64 offsetUs = timelineUs - clip.timelineStartUs;
                const qint64 sourceUs =
                    clip.sourceInUs + static_cast<qint64>(offsetUs * speed);

                QImage frame = src->frameAt(sourceUs);
                if (frame.isNull()) {
                    continue;
                }
                frame = applyEffects(clip.id, frame);

                const Placement pl = placementFor(clip, frame);
                painter.setOpacity(pl.opacity);
                painter.drawImage(QRectF(pl.x, pl.y, pl.w, pl.h), frame);
            }
        }
        painter.end();

        return canvasToYuv(canvas);
    }

    VideoFrame CompositionEngine::canvasToYuv(const QImage &canvas) const {
        VideoFrame out;
        const int w = canvas.width();
        const int h = canvas.height();
        if (w <= 0 || h <= 0) {
            return out;
        }

        // RGBA → YUV420P（BT.601）。复用缓存的 sws 上下文：尺寸不变时不重建，
        // 避免每帧 sws_getContext/free 反复分配缩放表（导出逐帧转换的主要开销）。
        if (!m_yuvSws || w != m_yuvSwsW || h != m_yuvSwsH) {
            if (m_yuvSws) {
                sws_freeContext(m_yuvSws);
            }
            m_yuvSws = sws_getContext(w, h, AV_PIX_FMT_RGBA,
                                      w, h, AV_PIX_FMT_YUV420P,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
            m_yuvSwsW = w;
            m_yuvSwsH = h;
        }
        if (!m_yuvSws) {
            return out;
        }

        const int cw = w / 2;
        const int ch = h / 2;
        out.y.resize(w * h);
        out.u.resize(cw * ch);
        out.v.resize(cw * ch);
        out.width = w;
        out.height = h;

        const uint8_t *srcData[4] = {
            reinterpret_cast<const uint8_t *>(canvas.bits()), nullptr, nullptr, nullptr};
        int srcLinesize[4] = {static_cast<int>(canvas.bytesPerLine()), 0, 0, 0};

        uint8_t *dstData[4] = {
            reinterpret_cast<uint8_t *>(out.y.data()),
            reinterpret_cast<uint8_t *>(out.u.data()),
            reinterpret_cast<uint8_t *>(out.v.data()),
            nullptr};
        int dstLinesize[4] = {w, cw, cw, 0};

        sws_scale(m_yuvSws, srcData, srcLinesize, 0, h, dstData, dstLinesize);
        return out;
    }

} // namespace Mixed::Player
