//
// Created by Anlk on 2026/6/2.
//

#include "../include/mainwindow.h"
#include "../include/topbar.h"
#include "../include/theme.h"
#include "../include/VideoListPage.h"
#include "../include/homepage.h"
#include "../include/EditorWindow.h"

#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#ifdef Q_OS_MACOS
#include "../include/mac_titlebar.h"
#endif

namespace Mixed {
    MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
        this->setWindowTitle(QStringLiteral("Mixed"));
        setStyleSheet(Theme::styleSheet());
        setupUi();
    }

    void MainWindow::setupUi() {
        auto *root = new QWidget(this);
        root->setObjectName("root");
        setCentralWidget(root);

        // 纵向布局：顶部 tab 菜单 + 下方页面堆栈。
        auto *layout = new QVBoxLayout(root);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(14);

        auto *topBar = new TopBar(root);
        layout->addWidget(topBar);

        m_pages = new QStackedWidget(root);
        m_pages->addWidget(new VideoListPage(root));   // 0 短剧（视频列表网格）
        m_homePage = new HomePage(root);
        m_pages->addWidget(m_homePage);                // 1 剪辑
        m_pages->addWidget(new QWidget(root));         // 2 模板（占位）
        layout->addWidget(m_pages, 1);

        connect(topBar, &TopBar::pageRequested, this, &MainWindow::switchPage);
        // 「开始创作」新建空工程（空 id）；点工程卡片打开对应工程。
        connect(m_homePage, &HomePage::createRequested, this,
                [this]() { openEditor(QString()); });
        connect(m_homePage, &HomePage::openProjectRequested, this,
                &MainWindow::openEditor);
    }

    void MainWindow::openEditor(const QString &projectId) {
        // 每次按 id 新建独立编辑器实例，关闭即销毁，确保打开的就是所选工程。
        if (m_editor) {
            m_editor->deleteLater();
            m_editor = nullptr;
        }
        m_editor = new EditorWindow(projectId);
        m_editor->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_editor, &EditorWindow::closed, this, &MainWindow::onEditorClosed);
        connect(m_editor, &QObject::destroyed, this, [this]() { m_editor = nullptr; });

        hide();
        m_editor->show();
        m_editor->raise();
        m_editor->activateWindow();
    }

    void MainWindow::onEditorClosed() {
        // 工程可能新建/重命名/改动，回主窗口刷新工程列表。
        if (m_homePage) {
            m_homePage->refreshProjects();
        }
        show();
        raise();
        activateWindow();
    }

    void MainWindow::showEvent(QShowEvent *event) {
        QMainWindow::showEvent(event);
#ifdef Q_OS_MACOS
        // Inject the brand label and action icons into the native title bar the
        // first time the native window handle is available.
        if (!macTitleBarConfigured) {
            macTitleBarConfigured = true;
            configureMacTitleBar(reinterpret_cast<void *>(window()->winId()));
        }
#endif
    }

    void MainWindow::switchPage(int index) {
        if (m_pages && index >= 0 && index < m_pages->count()) {
            m_pages->setCurrentIndex(index);
        }
    }

    MainWindow::~MainWindow() = default;
} // Mixed
