//
// Created by Anlk on 2026/6/7.
// ExportQueueManager 实现。
//

#include "../include/ExportQueueManager.h"

#include "../../db/include/Database.h"

#include <QThread>

namespace Mixed {

    ExportQueueManager::ExportQueueManager(QObject *parent) : QObject(parent) {}

    ExportQueueManager::~ExportQueueManager() {
        shutdown();
    }

    void ExportQueueManager::enqueue(const Player::Exporter::Request &req,
                                     const QString &projectId, const QString &sequenceId,
                                     const QString &displayName) {
        Job job;
        job.req = req;
        job.displayName = displayName;

        DB::ExportJob &m = job.model;
        m.projectId = projectId;
        m.sequenceId = sequenceId;
        m.status = QStringLiteral("pending");
        m.outputPath = req.outputPath;
        m.format = QStringLiteral("mp4");
        m.videoCodec = QStringLiteral("h264");
        m.audioCodec = QStringLiteral("aac");
        m.width = req.width;
        m.height = req.height;
        m.fps = req.fps;
        m.bitrate = req.videoBitrate;
        m.progress = 0.0;
        m_jobRepo.insert(m);   // 分配 id + created_at

        m_jobs.push_back(job);
        emit jobAdded(m);
        startNextIfIdle();
    }

    void ExportQueueManager::cancelJob(const QString &jobId) {
        // 运行中任务：通知 worker 取消（onFinished 会收尾）。
        if (m_activeExporter && m_activeJob.id == jobId) {
            m_activeExporter->cancel();
            return;
        }
        // 排队中任务：直接标记 canceled 并移出队列。
        for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
            if (it->model.id == jobId) {
                it->model.status = QStringLiteral("canceled");
                it->model.finishedAt = DB::Database::nowMs();
                m_jobRepo.update(it->model);
                const DB::ExportJob snapshot = it->model;
                m_jobs.erase(it);
                emit jobUpdated(snapshot);
                return;
            }
        }
    }

    void ExportQueueManager::startNextIfIdle() {
        if (m_activeThread) {
            return;  // 已有任务在跑
        }
        if (m_jobs.empty()) {
            return;
        }

        Job job = std::move(m_jobs.front());
        m_jobs.pop_front();

        m_activeJob = job.model;
        m_activeJob.status = QStringLiteral("running");
        m_activeJob.startedAt = DB::Database::nowMs();
        m_jobRepo.update(m_activeJob);
        m_lastPersistedPct = -1;
        emit jobUpdated(m_activeJob);

        const QString jobId = m_activeJob.id;

        m_activeThread = new QThread(this);
        m_activeExporter = new Player::Exporter;   // 无父对象，moveToThread
        m_activeExporter->setRequest(job.req);
        m_activeExporter->moveToThread(m_activeThread);

        connect(m_activeThread, &QThread::started, m_activeExporter, &Player::Exporter::run);
        connect(m_activeExporter, &Player::Exporter::progress, this,
                [this, jobId](int pct) { onProgress(jobId, pct); }, Qt::QueuedConnection);
        connect(m_activeExporter, &Player::Exporter::finished, this,
                [this, jobId](bool ok, const QString &msg) { onFinished(jobId, ok, msg); },
                Qt::QueuedConnection);

        m_activeThread->start();
    }

    void ExportQueueManager::onProgress(const QString &jobId, int percent) {
        if (m_activeJob.id != jobId) {
            return;
        }
        m_activeJob.progress = percent / 100.0;
        // 落库节流：每 +5% 或到达 100% 才写库；UI 信号每次都发（廉价）。
        if (percent >= 100 || percent - m_lastPersistedPct >= 5) {
            m_lastPersistedPct = percent;
            m_jobRepo.update(m_activeJob);
        }
        emit jobUpdated(m_activeJob);
    }

    void ExportQueueManager::onFinished(const QString &jobId, bool ok, const QString &message) {
        if (m_activeJob.id != jobId) {
            return;
        }
        // 区分“取消”与“失败”：取消由 cancel() 触发，finished(false, "已取消导出。")。
        const bool canceled = !ok && message.contains(QStringLiteral("取消"));
        m_activeJob.status = canceled ? QStringLiteral("canceled")
                          : ok        ? QStringLiteral("done")
                                      : QStringLiteral("failed");
        m_activeJob.finishedAt = DB::Database::nowMs();
        if (ok) {
            m_activeJob.progress = 1.0;
            m_activeJob.outputPath = message;   // 成功时 message 为输出路径
        } else if (!canceled) {
            m_activeJob.errorMessage = message;
        }
        m_jobRepo.update(m_activeJob);
        const DB::ExportJob snapshot = m_activeJob;

        teardownActive();
        emit jobUpdated(snapshot);

        startNextIfIdle();
    }

    void ExportQueueManager::teardownActive() {
        if (m_activeThread) {
            m_activeThread->quit();
            m_activeThread->wait();
            delete m_activeThread;
            m_activeThread = nullptr;
        }
        delete m_activeExporter;
        m_activeExporter = nullptr;
        m_activeJob = DB::ExportJob{};
    }

    void ExportQueueManager::shutdown() {
        // 标记仍在排队的任务为 canceled。
        for (Job &job : m_jobs) {
            job.model.status = QStringLiteral("canceled");
            job.model.finishedAt = DB::Database::nowMs();
            m_jobRepo.update(job.model);
        }
        m_jobs.clear();

        // 取消并 join 正在跑的任务。
        if (m_activeExporter) {
            m_activeExporter->cancel();
        }
        if (m_activeThread) {
            m_activeThread->quit();
            m_activeThread->wait();
            delete m_activeThread;
            m_activeThread = nullptr;
        }
        delete m_activeExporter;
        m_activeExporter = nullptr;
    }

} // namespace Mixed
