//
// Created by Anlk on 2026/6/2.
//

#include "../include/mainwindow.h"
#include "../include/sidebar.h"
#include "../include/theme.h"
#include "../include/PlayerPage.h"

#include <QHBoxLayout>
#include <QShowEvent>
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

        layout->addWidget(new Sidebar(root));
        layout->addWidget(new Player::PlayerPage(root), /*stretch=*/1);
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

    MainWindow::~MainWindow() = default;
} // Mixed
