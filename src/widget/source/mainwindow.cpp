//
// Created by Anlk on 2026/6/2.
//

#include "../include/mainwindow.h"
#include "../include/topbar.h"
#include "../include/theme.h"
#include "../include/VideoListPage.h"
#include "../include/homepage.h"

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
        m_pages->addWidget(new HomePage(root));        // 1 剪辑
        m_pages->addWidget(new QWidget(root));         // 2 模板（占位）
        layout->addWidget(m_pages, 1);

        connect(topBar, &TopBar::pageRequested, this, &MainWindow::switchPage);
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
