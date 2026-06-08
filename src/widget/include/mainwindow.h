//
// Created by Anlk on 2026/6/2.
//

#ifndef MIXEDEDITING_MAINWINDOW_H
#define MIXEDEDITING_MAINWINDOW_H

#include <QMainWindow>

class QStackedWidget;

namespace Mixed {
    class EditorWindow;
    class HomePage;

    class MainWindow : public QMainWindow {
        Q_OBJECT

    public:
        explicit MainWindow(QWidget *parent = nullptr);

        ~MainWindow() override;

    protected:
        void showEvent(QShowEvent *event) override;

    private slots:
        void switchPage(int index);
        void openEditor(const QString &projectId); // 打开指定工程（空=新建）的剪辑工作区
        void onEditorClosed();  // 剪辑窗口关闭后恢复主窗口

    private:
        void setupUi();

        QStackedWidget *m_pages = nullptr;
        EditorWindow *m_editor = nullptr;
        HomePage *m_homePage = nullptr;
        bool macTitleBarConfigured = false;
    };
} // Mixed

#endif //MIXEDEDITING_MAINWINDOW_H
