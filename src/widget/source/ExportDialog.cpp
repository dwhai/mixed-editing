//
// Created by Anlk on 2026/6/7.
// ExportDialog 实现。
//

#include "../include/ExportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace Mixed {

    namespace {
        // 各画质档的“每像素每帧比特”系数：码率 = w*h*fps*bpp。
        // 高≈0.15(1080p30→~9.3Mbps) / 中≈0.08(~5Mbps) / 低≈0.04(~2.5Mbps)。
        constexpr double kBppHigh = 0.15;
        constexpr double kBppMed  = 0.08;
        constexpr double kBppLow  = 0.04;

        int makeEven(int v) { return v - (v & 1); }
    } // namespace

    ExportDialog::ExportDialog(int projW, int projH, double projFps,
                               const QString &defaultBaseName, QWidget *parent)
        : QDialog(parent), m_projW(projW > 0 ? projW : 1920),
          m_projH(projH > 0 ? projH : 1080), m_projFps(projFps > 0 ? projFps : 30.0) {
        setWindowTitle(QStringLiteral("导出设置"));
        setModal(true);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(20, 18, 20, 16);
        root->setSpacing(14);

        auto *form = new QFormLayout();
        form->setSpacing(10);

        // 分辨率档：工程默认 + 1080p/720p/480p（按工程宽高比换算）。
        m_resCombo = new QComboBox(this);
        m_resCombo->addItem(QStringLiteral("工程默认 (%1 × %2)").arg(m_projW).arg(m_projH));
        for (int idx = 1; idx <= 3; ++idx) {
            int w = 0, h = 0;
            resolutionFor(idx, w, h);
            const char *tag = (idx == 1) ? "1080p" : (idx == 2) ? "720p" : "480p";
            m_resCombo->addItem(QStringLiteral("%1 (%2 × %3)").arg(tag).arg(w).arg(h));
        }
        form->addRow(QStringLiteral("分辨率"), m_resCombo);

        // 码率档。
        m_qualityCombo = new QComboBox(this);
        m_qualityCombo->addItem(QStringLiteral("高（更清晰，文件大）"));
        m_qualityCombo->addItem(QStringLiteral("中（推荐）"));
        m_qualityCombo->addItem(QStringLiteral("低（更小，画质一般）"));
        m_qualityCombo->setCurrentIndex(1);
        form->addRow(QStringLiteral("画质"), m_qualityCombo);

        root->addLayout(form);

        // 输出路径：只读输入框 + 浏览。默认目录记忆于 QSettings。
        QSettings settings(QStringLiteral("MixedEditing"), QStringLiteral("MixedEditing"));
        const QString lastDir =
            settings.value(QStringLiteral("paths/lastExportDir")).toString();
        QString base = defaultBaseName.isEmpty() ? QStringLiteral("export") : defaultBaseName;
        const QString dir = lastDir.isEmpty()
            ? QDir::homePath()
            : lastDir;
        const QString defaultPath = QDir(dir).filePath(base + QStringLiteral(".mp4"));

        auto *pathLabel = new QLabel(QStringLiteral("输出文件"), this);
        m_pathEdit = new QLineEdit(defaultPath, this);
        m_pathEdit->setMinimumWidth(360);
        auto *browse = new QPushButton(QStringLiteral("浏览…"), this);
        browse->setCursor(Qt::PointingHandCursor);
        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString chosen = QFileDialog::getSaveFileName(
                this, QStringLiteral("导出视频"), m_pathEdit->text(),
                QStringLiteral("MP4 视频 (*.mp4)"));
            if (!chosen.isEmpty()) {
                m_pathEdit->setText(chosen);
            }
        });
        auto *pathRow = new QHBoxLayout();
        pathRow->setSpacing(8);
        pathRow->addWidget(m_pathEdit, 1);
        pathRow->addWidget(browse);

        root->addWidget(pathLabel);
        root->addLayout(pathRow);

        // 确定/取消。
        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始导出"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (collectResult()) accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }

    void ExportDialog::resolutionFor(int idx, int &w, int &h) const {
        if (idx <= 0) {  // 工程默认
            w = m_projW;
            h = m_projH;
            return;
        }
        const int targetH = (idx == 1) ? 1080 : (idx == 2) ? 720 : 480;
        // 按工程宽高比换算宽度，偶数对齐（H.264 要求）。
        const double aspect = static_cast<double>(m_projW) / std::max(1, m_projH);
        h = targetH;
        w = makeEven(static_cast<int>(std::lround(targetH * aspect)));
        if (w <= 0) w = makeEven(m_projW);
        // 不放大超过工程原始分辨率：若工程本身更小，则取工程值。
        if (targetH > m_projH) {
            w = m_projW;
            h = m_projH;
        }
    }

    bool ExportDialog::collectResult() {
        resolutionFor(m_resCombo->currentIndex(), m_outW, m_outH);
        m_outW = std::max(2, makeEven(m_outW));
        m_outH = std::max(2, makeEven(m_outH));

        const double bpp = (m_qualityCombo->currentIndex() == 0) ? kBppHigh
                         : (m_qualityCombo->currentIndex() == 1) ? kBppMed
                         : kBppLow;
        double br = static_cast<double>(m_outW) * m_outH * m_projFps * bpp;
        m_outBitrate = std::clamp(static_cast<int>(br), 500'000, 60'000'000);

        QString path = m_pathEdit->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("导出"),
                                 QStringLiteral("请填写输出文件路径。"));
            return false;
        }
        if (!path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".mp4");
        }
        m_outPath = path;

        // 记忆导出目录。
        QSettings settings(QStringLiteral("MixedEditing"), QStringLiteral("MixedEditing"));
        settings.setValue(QStringLiteral("paths/lastExportDir"),
                          QFileInfo(path).absolutePath());
        return true;
    }

} // namespace Mixed
