//
// Created by Anlk on 2026/6/6.
// Exporter：把时间线合成结果导出为 MP4（H.264 视频 + AAC 音频）。
// 作为 QObject worker 跑在独立线程：视频用 CompositionEngine 逐帧 composeAt → 编码；
// 音频把各音频片段解码+重采样后按时间线起点混入一条整轨缓冲 → 编码；
// 两路按 pts 交错写入 muxer。通过 progress/finished 信号回主线程更新 UI。
//
// 自带独立的 CompositionEngine 与解码器，不与主线程预览共享对象，线程安全。
//

#ifndef MIXEDEDITING_EXPORTER_H
#define MIXEDEDITING_EXPORTER_H

#include <QObject>
#include <QString>
#include <QHash>
#include <atomic>
#include <vector>

#include "../../db/include/DbModels.h"

namespace Mixed::Player {

    class Exporter : public QObject {
        Q_OBJECT
    public:
        // 一次导出任务的全部输入（在主线程快照好后传入，worker 不再访问数据库）。
        struct Request {
            QString outputPath;
            int     width = 1920;
            int     height = 1080;
            int     fps = 30;
            int     videoBitrate = 6'000'000;   // H.264 目标码率（bps），由导出对话框画质档决定
            qint64  durationUs = 0;
            std::vector<DB::Track> tracks;                       // 按 track_index 升序
            QHash<QString, std::vector<DB::Clip>> clipsByTrack;  // track.id -> clips
            QHash<QString, QString> assetPathByClip;             // clip.id -> 源文件路径
        };

        explicit Exporter(QObject *parent = nullptr);
        ~Exporter() override;

        // 设置任务（在 start 前于主线程调用）。
        void setRequest(const Request &req) { m_req = req; }
        // 请求取消（线程安全标志）。
        void cancel() { m_cancelled.store(true); }

    public slots:
        // 在 worker 线程执行整个导出流程。
        void run();

    signals:
        void progress(int percent);                 // 0~100
        void finished(bool ok, const QString &message);

    private:
        Request m_req;
        std::atomic<bool> m_cancelled{false};
    };

} // namespace Mixed::Player

#endif // MIXEDEDITING_EXPORTER_H
