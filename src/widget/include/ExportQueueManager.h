//
// Created by Anlk on 2026/6/7.
// ExportQueueManager：导出任务队列管理器（GUI 线程）。
// 维护一个 FIFO 队列，一次只跑一个 Exporter worker（QThread），跑完自动取下一个。
// 每个任务落库到 export_job（pending/running/done/failed/canceled + progress），
// 通过 jobAdded/jobUpdated 信号驱动 ExportQueuePanel 的非阻塞 UI。
// 所有数据库访问都在 GUI 线程，worker 仅收到 Request 值快照（线程安全）。
//

#ifndef MIXEDEDITING_EXPORTQUEUEMANAGER_H
#define MIXEDEDITING_EXPORTQUEUEMANAGER_H

#include <QObject>
#include <QString>
#include <deque>

#include "../../db/include/DbModels.h"
#include "../../db/include/Repositories.h"
#include "../../ffmpeg/include/Exporter.h"

class QThread;

namespace Mixed {

    class ExportQueueManager : public QObject {
        Q_OBJECT

    public:
        explicit ExportQueueManager(QObject *parent = nullptr);
        ~ExportQueueManager() override;

        // 入队一个导出任务：构建并落库 export_job，发 jobAdded，必要时立即启动。
        // displayName 用于 UI 行标题（通常取输出文件名）。
        void enqueue(const Player::Exporter::Request &req,
                     const QString &projectId, const QString &sequenceId,
                     const QString &displayName);

        // 取消指定任务：运行中 → 通知 worker 取消；排队中 → 直接标记 canceled 出队。
        void cancelJob(const QString &jobId);

        // 关窗收尾：取消运行中任务并 join 活动线程，确保无悬挂。
        void shutdown();

        bool isBusy() const { return m_activeThread != nullptr; }

    signals:
        void jobAdded(const DB::ExportJob &job);
        void jobUpdated(const DB::ExportJob &job);

    private:
        struct Job {
            DB::ExportJob model;             // 落库行（持久状态）
            Player::Exporter::Request req;   // 内存输入快照（不落库）
            QString displayName;
        };

        void startNextIfIdle();
        void onProgress(const QString &jobId, int percent);
        void onFinished(const QString &jobId, bool ok, const QString &message);
        void teardownActive();

        std::deque<Job> m_jobs;              // 待跑队列（不含正在跑的）
        QThread          *m_activeThread = nullptr;
        Player::Exporter *m_activeExporter = nullptr;
        DB::ExportJob     m_activeJob;       // 正在跑的任务行
        int               m_lastPersistedPct = -1; // 落库节流：上次写库的百分比

        DB::ExportJobRepository m_jobRepo;
    };

} // namespace Mixed

#endif // MIXEDEDITING_EXPORTQUEUEMANAGER_H
