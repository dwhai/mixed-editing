//
// Scrollable home content: banner, function grid, upgrade cards and local drafts.
//

#ifndef MIXEDEDITING_HOMEPAGE_H
#define MIXEDEDITING_HOMEPAGE_H

#include <QWidget>

namespace Mixed {
    class HomePage : public QWidget {
        Q_OBJECT

    public:
        explicit HomePage(QWidget *parent = nullptr);

    signals:
        // 「开始创作」被点击，请求打开剪辑工作区。
        void createRequested();

    private:
        QWidget *buildBanner();
    };
} // Mixed

#endif //MIXEDEDITING_HOMEPAGE_H
