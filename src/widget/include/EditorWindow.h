//
// Created by Anlk on 2026/6/6.
// 视频剪辑窗口：独立的全屏工作区，从主窗口「开始创作」进入。
// 打开时创建一个工程 + 主序列 + 默认视频/音频轨道（落库）；导入素材写入 media_asset
// 并刷新素材库；双击素材在视频轨末尾追加片段（clip）并刷新时间线。
// 布局结构在此构建，视觉样式统一由 Theme 提供。
// 关闭时发出 closed() 信号，由 MainWindow 据此恢复主窗口显示。
//

#ifndef MIXEDEDITING_EDITORWINDOW_H
#define MIXEDEDITING_EDITORWINDOW_H

#include <QMainWindow>
#include <QString>

#include "../../db/include/DbModels.h"
#include "../../db/include/Repositories.h"

class QLabel;
class QVBoxLayout;

namespace Mixed {

    class EditorWindow : public QMainWindow {
        Q_OBJECT

    public:
        explicit EditorWindow(QWidget *parent = nullptr);
        ~EditorWindow() override;

    signals:
        // 窗口关闭时发出，MainWindow 接此信号重新显示主界面。
        void closed();

    protected:
        void closeEvent(QCloseEvent *event) override;

    private:
        void setupUi();
        QWidget *buildTopBar();      // 顶部：返回 + 工程名 + 导出
        QWidget *buildMediaPanel();  // 左侧：素材库
        QWidget *buildPreviewArea(); // 中间：预览画面
        QWidget *buildTimeline();    // 底部：时间线轨道

        // 数据层
        void ensureProject();        // 惰性创建工程 + 主序列 + 默认轨道（首次导入/添加片段时触发）
        void importMedia();          // 选文件 → 探测 → 落库 → 刷新
        void addClipFromAsset(const DB::MediaAsset &asset); // 追加片段到视频/音频轨
        void refreshMediaList();     // 重绘素材库
        void refreshTimeline();      // 重绘时间线片段

        // 仓库
        DB::ProjectRepository    m_projectRepo;
        DB::MediaAssetRepository m_assetRepo;
        DB::SequenceRepository   m_sequenceRepo;
        DB::TrackRepository      m_trackRepo;
        DB::ClipRepository       m_clipRepo;

        // 当前工程上下文
        DB::Project  m_project;
        DB::Sequence m_sequence;
        QString      m_videoTrackId;
        QString      m_audioTrackId;

        // 动态填充的容器
        QWidget     *m_mediaList = nullptr;
        QVBoxLayout *m_mediaListLayout = nullptr;
        QLabel      *m_projectTitle = nullptr;
        QWidget     *m_timelineTracks = nullptr;
        QVBoxLayout *m_timelineLayout = nullptr;

        bool m_closedEmitted = false; // 防止 closeEvent 重复发信号
    };

} // namespace Mixed

#endif // MIXEDEDITING_EDITORWINDOW_H
