//
// Left navigation sidebar.
//

#ifndef MIXEDEDITING_SIDEBAR_H
#define MIXEDEDITING_SIDEBAR_H

#include <QFrame>

namespace Mixed {
    class Sidebar : public QFrame {
        Q_OBJECT

    public:
        explicit Sidebar(QWidget *parent = nullptr);

    private:
        QWidget *buildAccountCard();
        QWidget *buildDiscoverCard();
    };
} // Mixed

#endif //MIXEDEDITING_SIDEBAR_H
