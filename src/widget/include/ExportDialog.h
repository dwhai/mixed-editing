//
// Created by Anlk on 2026/6/7.
// ExportDialog：导出前的画质与输出设置对话框。
// 提供分辨率档（工程默认 / 1080p / 720p / 480p，按工程宽高比换算）+ 码率档（高/中/低）
// + 输出路径（默认名含 UUID+时间戳，目录记忆于 QSettings）。EditorWindow 取其结果填入
// Exporter::Request。
//

#ifndef MIXEDEDITING_EXPORTDIALOG_H
#define MIXEDEDITING_EXPORTDIALOG_H

#include <QDialog>
#include <QString>

class QComboBox;
class QLineEdit;

namespace Mixed {

    class ExportDialog : public QDialog {
        Q_OBJECT

    public:
        // projW/projH/projFps：工程分辨率与帧率（用于“工程默认”档与码率换算）。
        // defaultBaseName：建议的文件名主体（不含扩展名，已含 UUID+时间戳）。
        ExportDialog(int projW, int projH, double projFps,
                     const QString &defaultBaseName, QWidget *parent = nullptr);

        // 用户选择结果（仅在 exec() 返回 Accepted 后有效）。
        int     outWidth() const { return m_outW; }
        int     outHeight() const { return m_outH; }
        int     outBitrate() const { return m_outBitrate; }
        QString outputPath() const { return m_outPath; }

    private:
        // 依据当前下拉选项计算输出宽高/码率/路径，成功返回 true。
        bool collectResult();
        // 选中分辨率档对应的输出宽高（保持工程宽高比，偶数对齐）。
        void resolutionFor(int idx, int &w, int &h) const;

        int    m_projW;
        int    m_projH;
        double m_projFps;

        QComboBox *m_resCombo = nullptr;
        QComboBox *m_qualityCombo = nullptr;
        QLineEdit *m_pathEdit = nullptr;

        int     m_outW = 1920;
        int     m_outH = 1080;
        int     m_outBitrate = 6'000'000;
        QString m_outPath;
    };

} // namespace Mixed

#endif // MIXEDEDITING_EXPORTDIALOG_H
