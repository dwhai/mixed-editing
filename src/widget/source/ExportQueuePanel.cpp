//
// Created by Anlk on 2026/6/7.
// ExportQueuePanel 实现。
//

#include "../include/ExportQueuePanel.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace Mixed {

    ExportQueuePanel::ExportQueuePanel(QWidget *parent) : QFrame(parent) {
        setObjectName("exportQueuePanel");

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(14, 8, 14, 10);
        root->setSpacing(6);

        m_heading = new QLabel(QStringLiteral("导出队列"), this);
        m_heading->setObjectName("editorPanelTitle");
        root->addWidget(m_heading);

        m_rowsLayout = new QVBoxLayout();
        m_rowsLayout->setContentsMargins(0, 0, 0, 0);
        m_rowsLayout->setSpacing(4);
        root->addLayout(m_rowsLayout);

        hide();  // 空队列时隐藏
    }

    QString ExportQueuePanel::statusText(const QString &status) {
        if (status == QStringLiteral("pending"))  return QStringLiteral("排队中");
        if (status == QStringLiteral("running"))  return QStringLiteral("导出中");
        if (status == QStringLiteral("done"))     return QStringLiteral("完成");
        if (status == QStringLiteral("failed"))   return QStringLiteral("失败");
        if (status == QStringLiteral("canceled")) return QStringLiteral("已取消");
        return status;
    }

    ExportQueuePanel::Row *ExportQueuePanel::ensureRow(const DB::ExportJob &job) {
        auto it = m_rows.find(job.id);
        if (it != m_rows.end()) {
            return &it.value();
        }

        Row row;
        row.container = new QWidget(this);
        auto *h = new QHBoxLayout(row.container);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);

        row.title = new QLabel(QFileInfo(job.outputPath).fileName(), row.container);
        row.title->setMinimumWidth(160);
        row.title->setToolTip(job.outputPath);

        row.status = new QLabel(statusText(job.status), row.container);
        row.status->setObjectName("editorHintLine");
        row.status->setMinimumWidth(48);

        row.bar = new QProgressBar(row.container);
        row.bar->setRange(0, 100);
        row.bar->setValue(0);
        row.bar->setTextVisible(true);

        row.cancel = new QPushButton(QStringLiteral("取消"), row.container);
        row.cancel->setCursor(Qt::PointingHandCursor);

        h->addWidget(row.title);
        h->addWidget(row.status);
        h->addWidget(row.bar, 1);
        h->addWidget(row.cancel);

        m_rowsLayout->addWidget(row.container);
        it = m_rows.insert(job.id, row);
        return &it.value();
    }

    void ExportQueuePanel::applyJob(Row *row, const DB::ExportJob &job) {
        row->status->setText(statusText(job.status));
        row->bar->setValue(static_cast<int>(job.progress * 100.0 + 0.5));

        const bool terminal = job.status == QStringLiteral("done") ||
                              job.status == QStringLiteral("failed") ||
                              job.status == QStringLiteral("canceled");
        // 成功时进度条置满。
        if (job.status == QStringLiteral("done")) {
            row->bar->setValue(100);
        }

        // 重设按钮行为：运行中=「取消」(发 cancelRequested)；终态=「清除」(删除该行)。
        // 每次都先断开旧连接，避免重复触发。
        const QString jobId = job.id;
        row->cancel->disconnect();
        if (terminal) {
            row->cancel->setText(QStringLiteral("清除"));
            row->cancel->setEnabled(true);
            row->cancel->setVisible(true);
            connect(row->cancel, &QPushButton::clicked, this,
                    [this, jobId]() { dismissRow(jobId); });
        } else {
            row->cancel->setText(QStringLiteral("取消"));
            row->cancel->setEnabled(true);
            row->cancel->setVisible(true);
            connect(row->cancel, &QPushButton::clicked, this,
                    [this, jobId]() { emit cancelRequested(jobId); });
        }

        if (!job.errorMessage.isEmpty()) {
            row->status->setToolTip(job.errorMessage);
        }
    }

    void ExportQueuePanel::dismissRow(const QString &jobId) {
        auto it = m_rows.find(jobId);
        if (it == m_rows.end()) {
            return;
        }
        if (it.value().container) {
            it.value().container->deleteLater();
        }
        m_rows.erase(it);
        updateVisibility();   // 清空后面板自动隐藏
    }

    void ExportQueuePanel::onJobAdded(const DB::ExportJob &job) {
        Row *row = ensureRow(job);
        applyJob(row, job);
        updateVisibility();
    }

    void ExportQueuePanel::onJobUpdated(const DB::ExportJob &job) {
        Row *row = ensureRow(job);
        applyJob(row, job);
        updateVisibility();
    }

    void ExportQueuePanel::updateVisibility() {
        setVisible(!m_rows.isEmpty());
    }

} // namespace Mixed
