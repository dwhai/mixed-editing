//
// Created by Anlk on 2026/6/2.
//

#include "../include/mainwindow.h"
#include "../include/sidebar.h"
#include "../include/theme.h"
#include "../include/PlayerPage.h"
#include "../include/homepage.h"

#include <QHBoxLayout>
#include <QShowEvent>
#include <QStackedWidget>
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

        auto *layout = new QHBoxLayout(root);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(14);

        auto *sidebar = new Sidebar(root);
        layout->addWidget(sidebar);

        m_pages = new QStackedWidget(root);
        m_pages->addWidget(new HomePage(root));
        m_pages->addWidget(new QWidget(root));
        m_pages->addWidget(new Player::PlayerPage(root));
        layout->addWidget(m_pages, 1);

        connect(sidebar, &Sidebar::pageRequested, this, &MainWindow::switchPage);
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
