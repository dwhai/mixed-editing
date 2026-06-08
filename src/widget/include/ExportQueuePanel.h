//
// Created by Anlk on 2026/6/7.
// ExportQueuePanel：导出队列的非阻塞 UI 条，停靠在时间线下方。
// 每个任务一行：名称 + 状态 + 进度条 + 取消按钮。队列为空时整体隐藏，导出期间用户
// 仍可自由编辑/播放。仅展示与转发取消意图，不触碰数据库或线程。
//

#ifndef MIXEDEDITING_EXPORTQUEUEPANEL_H
#define MIXEDEDITING_EXPORTQUEUEPANEL_H

#include <QFrame>
#include <QHash>
#include <QString>

#include "../../db/include/DbModels.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace Mixed {

    class ExportQueuePanel : public QFrame {
        Q_OBJECT

    public:
        explicit ExportQueuePanel(QWidget *parent = nullptr);

    public slots:
        void onJobAdded(const DB::ExportJob &job);
        void onJobUpdated(const DB::ExportJob &job);

    signals:
        void cancelRequested(const QString &jobId);

    private:
        // 单个任务行的控件集合。
        struct Row {
            QWidget      *container = nullptr;
            QLabel       *title = nullptr;
            QLabel       *status = nullptr;
            QProgressBar *bar = nullptr;
            QPushButton  *cancel = nullptr;
        };

        Row *ensureRow(const DB::ExportJob &job);
        void applyJob(Row *row, const DB::ExportJob &job);
        void dismissRow(const QString &jobId);   // 删除某行（终态后「清除」用）
        void updateVisibility();
        static QString statusText(const QString &status);

        QLabel      *m_heading = nullptr;
        QVBoxLayout *m_rowsLayout = nullptr;
        QHash<QString, Row> m_rows;   // jobId -> Row
    };

} // namespace Mixed

#endif // MIXEDEDITING_EXPORTQUEUEPANEL_H
