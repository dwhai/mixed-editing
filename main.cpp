#include <QApplication>
#include "src/widget/include/mainwindow.h"
#include "src/db/include/Database.h"
#include <QScreen>
#include <QRect>
#include <QStyleHints>

using namespace Mixed;
int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setApplicationName("Mixed");
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);

    // 打开剪辑数据层（WAL + 迁移），失败不阻断启动，仅记录告警。
    DB::Database::instance().open();

    MainWindow w;
    w.resize(1280, 760);
    // 设置窗口图标
    w.setWindowIcon(QIcon(":/icon.png"));
    // 居中显示窗口
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();
    int x = (screenGeometry.width() - w.width()) / 2;
    int y = (screenGeometry.height() - w.height()) / 2;
    w.move(x, y);
    w.show();
    return QApplication::exec();
}