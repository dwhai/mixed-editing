//
// Created by Anlk on 2026/6/2.
//

#ifndef MIXEDEDITING_MAINWINDOW_H
#define MIXEDEDITING_MAINWINDOW_H

#include <QMainWindow>

class QStackedWidget;

namespace Mixed {
    class MainWindow : public QMainWindow {
        Q_OBJECT

    public:
        explicit MainWindow(QWidget *parent = nullptr);

        ~MainWindow() override;

    protected:
        void showEvent(QShowEvent *event) override;

    private slots:
        void switchPage(int index);

    private:
        void setupUi();

        QStackedWidget *m_pages = nullptr;
        bool macTitleBarConfigured = false;
    };
} // Mixed

#endif //MIXEDEDITING_MAINWINDOW_H
