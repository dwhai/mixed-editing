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

        // 预排序：仅保留可见视频轨，按 track_index 升序（小者在底层先画）。
        // 每帧不再重排，composeAt 直接遍历。
        m_videoTracksSorted.clear();
        for (const DB::Track &t : tracks) {
            if (t.trackType == QStringLiteral("video") && !t.isHidden) {
                m_videoTracksSorted.push_back(t);
            }
        }
        std::sort(m_videoTracksSorted.begin(), m_videoTracksSorted.end(),
                  [](const DB::Track &a, const DB::Track &b) {
                      return a.trackIndex < b.trackIndex;
                  });

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
        return placementForSize(clip, srcImage.width(), srcImage.height());
    }

    CompositionEngine::Placement CompositionEngine::placementForSize(const DB::Clip &clip,
                                                                     int srcWi, int srcHi) const {
        Placement p;
        p.opacity = std::clamp(clip.opacity, 0.0, 1.0);

        // 默认：素材按「contain」方式缩放铺满画布并居中（保持源宽高比）。
        const double srcW = srcWi;
        const double srcH = srcHi;
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
        // 收集本时刻命中的可见片段（已按轨道 track_index 升序：底层在前）。
        struct Hit { const DB::Clip *clip; ClipSource *src; qint64 sourceUs; };
        std::vector<Hit> hits;

        for (const DB::Track &track : m_videoTracksSorted) {
            auto clipsIt = m_clipsByTrack.find(track.id);
            if (clipsIt == m_clipsByTrack.end()) {
                continue;
            }
            for (const DB::Clip &clip : clipsIt->second) {
                if (!clip.isEnabled) continue;
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
                const double speed = clip.speed > 0.0 ? clip.speed : 1.0;
                const qint64 offsetUs = timelineUs - clip.timelineStartUs;
                const qint64 sourceUs = clip.sourceInUs + static_cast<qint64>(offsetUs * speed);
                hits.push_back({&clip, src, sourceUs});
            }
        }

        // 无命中：直接产黑色 YUV 帧，省去 RGBA 画布 + 转换。
        if (hits.empty()) {
            return blackYuv();
        }

        // 快路径：仅一个片段、全不透明、且 contain 后正好铺满画布（无黑边、无 PiP）。
        // 直接源 → YUV420P@画布尺寸 一次缩放，跳过 RGBA + QPainter + 二次转换。
        if (hits.size() == 1) {
            const Hit &h = hits.front();
            if (std::clamp(h.clip->opacity, 0.0, 1.0) >= 0.999) {
                const Placement pl = placementForSize(*h.clip, h.src->width(), h.src->height());
                const bool fillsCanvas =
                    pl.x <= 0.5 && pl.y <= 0.5 &&
                    pl.w >= m_canvasW - 0.5 && pl.h >= m_canvasH - 0.5 &&
                    std::abs(pl.x) < 1.0 && std::abs(pl.y) < 1.0;
                if (fillsCanvas) {
                    VideoFrame out;
                    out.width = m_canvasW;
                    out.height = m_canvasH;
                    if (h.src->frameYuvAt(h.sourceUs, m_canvasW, m_canvasH,
                                          out.y, out.u, out.v)) {
                        return out;
                    }
                    // 快路径失败（解码未命中等）→ 落回慢路径。
                }
            }
        }

        // 慢路径：多片段叠加 / 画中画 / 有黑边 / 透明度混合，用 QPainter 在 RGBA 画布合成。
        QImage canvas(m_canvasW, m_canvasH, QImage::Format_RGBA8888);
        canvas.fill(QColor(0, 0, 0, 255));

        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        for (const Hit &h : hits) {
            QImage frame = h.src->frameAt(h.sourceUs);
            if (frame.isNull()) {
                continue;
            }
            frame = applyEffects(h.clip->id, frame);
            const Placement pl = placementFor(*h.clip, frame);
            painter.setOpacity(pl.opacity);
            painter.drawImage(QRectF(pl.x, pl.y, pl.w, pl.h), frame);
        }
        painter.end();

        return canvasToYuv(canvas);
    }

    VideoFrame CompositionEngine::blackYuv() const {
        VideoFrame out;
        const int w = m_canvasW, h = m_canvasH;
        if (w <= 0 || h <= 0) return out;
        const int cw = w / 2, ch = h / 2;
        out.width = w;
        out.height = h;
        out.y = QByteArray(w * h, static_cast<char>(16));    // BT.601/709 limited 黑
        out.u = QByteArray(cw * ch, static_cast<char>(128));
        out.v = QByteArray(cw * ch, static_cast<char>(128));
        return out;
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
