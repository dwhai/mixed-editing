//
// Created by Anlk on 2026/6/7.
// AudioMix：时间线音频混流的可复用内核。
// 把所有音频轨片段解码 + 重采样到 48k 立体声 float，按时间线绝对样本位置叠加进
// 给定样本区间的缓冲。导出（Exporter）与实时播放（PlaybackController）共用此实现，
// 二者只需提供轨道/片段/源路径的值快照（不触数据库，线程安全）。
//

#ifndef MIXEDEDITING_AUDIOMIX_H
#define MIXEDEDITING_AUDIOMIX_H

#include <QHash>
#include <QString>
#include <atomic>
#include <vector>

#include "../../db/include/DbModels.h"

namespace Mixed::Player {

    // 混流目标格式常量（导出与实时播放统一）。
    constexpr int    kAudioRate = 48000;   // 采样率
    constexpr int    kAudioCh   = 2;       // 立体声
    constexpr qint64 kUsPerSec  = 1'000'000;

    // 混流所需的时间线快照（Exporter::Request 的音频子集）。值语义，跨线程安全。
    struct AudioMixSource {
        std::vector<DB::Track> tracks;                       // 全部轨道（仅 audio 轨参与）
        QHash<QString, std::vector<DB::Clip>> clipsByTrack;  // track.id -> clips
        QHash<QString, QString> assetPathByClip;             // clip.id -> 源文件路径
    };

    // 把 [startSample,endSample) 范围内、与该区间重叠的所有音频片段混进 out。
    // out 为该区间的交错 float 立体声缓冲（长度 = (end-start)*kAudioCh），函数内部 assign 清零。
    // cancelled 置位时尽快返回。各调用方只写自己区间，互不重叠、无需加锁。
    void mixAudioRange(const AudioMixSource &src,
                       qint64 startSample, qint64 endSample,
                       std::vector<float> &out,
                       std::atomic<bool> &cancelled);

} // namespace Mixed::Player

#endif // MIXEDEDITING_AUDIOMIX_H
