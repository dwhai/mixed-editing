#include <QApplication>
#include "./widget/include/mainwindow.h"
#include <QScreen>
#include <QRect>
#include <QStyleHints>

using namespace Mixed;
int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setApplicationName("Mixed");
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    MainWindow w;
    w.resize(1280, 760);
    
    // 居中显示窗口
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();
    int x = (screenGeometry.width() - w.width()) / 2;
    int y = (screenGeometry.height() - w.height()) / 2;
    w.move(x, y);
    w.show();
    return QApplication::exec();
}