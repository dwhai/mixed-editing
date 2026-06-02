//
// Created by Anlk on 2026/6/2.
//

#ifndef MIXEDEDITING_MAINWINDOW_H
#define MIXEDEDITING_MAINWINDOW_H

#include <QMainWindow>

namespace Mixed {
    QT_BEGIN_NAMESPACE

    namespace Ui {
        class MainWindow;
    }

    QT_END_NAMESPACE

    class MainWindow : public QMainWindow {
        Q_OBJECT

    public:
        explicit MainWindow(QWidget *parent = nullptr);

        ~MainWindow() override;

    protected:
        void showEvent(QShowEvent *event) override;

    private:
        Ui::MainWindow *ui;
        bool macTitleBarConfigured = false;
    };
} // Mixed

#endif //MIXEDEDITING_MAINWINDOW_H