//
// Created by Anlk on 2026/6/6.
// CompositionEngine：时间线合成引擎。给定时刻 t，取所有可见视频轨命中的片段，
// 按轨道图层序（track_index 小者在下）将各片段帧依 transform(位置/缩放) + opacity
// 叠加到工程画布(width×height)，输出 YUV420P 的 VideoFrame 供 GL 控件显示。
//
// 本阶段(P1)仅实现视频轨叠加 + 位置/缩放/不透明度。特效链、转场、关键帧在
// applyEffects()/转场逻辑处留有接口位，后续阶段(P6–P9)填充。
//

#ifndef MIXEDEDITING_COMPOSITIONENGINE_H
#define MIXEDEDITING_COMPOSITIONENGINE_H

#include "PlayerTypes.h"
#include "../../db/include/DbModels.h"

#include <QImage>
#include <QString>
#include <memory>
#include <unordered_map>
#include <vector>

// libswscale 的不透明类型，仅作指针成员用，前置声明避免在头里包含 C 头。
struct SwsContext;

namespace Mixed::Player {

    class ClipSource;

    class CompositionEngine {
    public:
        CompositionEngine();
        ~CompositionEngine();

        // 画布尺寸（工程分辨率）。
        void setCanvasSize(int width, int height);

        // 设置当前序列的轨道与片段快照，并提供 clip→素材文件路径的解析。
        // tracks 应按 track_index 升序；clipsByTrack 以 track.id 为键。
        // assetPathByClip 给出每个 clip.id 对应的源文件绝对路径（用于打开 ClipSource）。
        void setTimeline(const std::vector<DB::Track> &tracks,
                         const std::unordered_map<QString, std::vector<DB::Clip>> &clipsByTrack,
                         const std::unordered_map<QString, QString> &assetPathByClip);

        // 合成 timelineUs 时刻的画面，返回 YUV420P 帧。无内容时返回黑帧。
        VideoFrame composeAt(qint64 timelineUs);

        // 清空 ClipSource 池（素材变更时调用）。
        void clearSources();

    private:
        // clip.transform JSON → 在画布上的目标矩形与不透明度。
        struct Placement {
            double x = 0.0;       // 画布像素坐标（左上角）
            double y = 0.0;
            double w = 0.0;       // 目标宽高
            double h = 0.0;
            double opacity = 1.0;
        };

        ClipSource *sourceFor(const QString &clipId, const QString &path);
        Placement placementFor(const DB::Clip &clip, const QImage &srcImage) const;
        // 不依赖已解码图像、仅用源宽高计算 placement（快路径预判铺满画布用）。
        Placement placementForSize(const DB::Clip &clip, int srcW, int srcH) const;
        VideoFrame canvasToYuv(const QImage &canvas) const;
        VideoFrame blackYuv() const;   // 画布尺寸的黑色 YUV420P 帧

        // 特效链占位：当前直接返回原图，后续阶段对接 libavfilter。
        QImage applyEffects(const QString &clipId, const QImage &src) const;

        int m_canvasW = 1920;
        int m_canvasH = 1080;

        std::vector<DB::Track> m_tracks;
        std::vector<DB::Track> m_videoTracksSorted;  // 仅视频轨、按 track_index 升序（setTimeline 时算好）
        std::unordered_map<QString, std::vector<DB::Clip>> m_clipsByTrack;
        std::unordered_map<QString, QString> m_assetPathByClip;

        // ClipSource 池：键为 clip.id（同一素材不同片段各有独立解码游标）。
        std::unordered_map<QString, std::unique_ptr<ClipSource>> m_sources;

        // RGBA→YUV420P 的 sws 上下文缓存：尺寸不变时跨帧复用，避免每帧重建
        // （sws_getContext 每次都分配缩放表，是导出逐帧转换的主要开销）。
        mutable SwsContext *m_yuvSws = nullptr;
        mutable int m_yuvSwsW = 0;
        mutable int m_yuvSwsH = 0;
    };

} // namespace Mixed::Player

#endif // MIXEDEDITING_COMPOSITIONENGINE_H
