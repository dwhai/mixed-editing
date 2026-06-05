//
// Left navigation sidebar.
//

#ifndef MIXEDEDITING_SIDEBAR_H
#define MIXEDEDITING_SIDEBAR_H

#include <QFrame>
#include <QVector>

class QPushButton;

namespace Mixed {
    class Sidebar : public QFrame {
        Q_OBJECT

    public:
        explicit Sidebar(QWidget *parent = nullptr);

    signals:
        void pageRequested(int index);

    private:
        QWidget *buildAccountCard();
        QWidget *buildDiscoverCard();
        void updateActiveButton(int index);

        QVector<QPushButton *> m_navButtons;
    };
} // Mixed

#endif //MIXEDEDITING_SIDEBAR_H
