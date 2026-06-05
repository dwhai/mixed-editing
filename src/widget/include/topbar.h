//
// Top navigation bar: horizontal tabs on the left, account entry on the right.
// Replaces the former left Sidebar.
//

#ifndef MIXEDEDITING_TOPBAR_H
#define MIXEDEDITING_TOPBAR_H

#include <QFrame>
#include <QVector>

class QPushButton;

namespace Mixed {
    class TopBar : public QFrame {
        Q_OBJECT

    public:
        explicit TopBar(QWidget *parent = nullptr);

    signals:
        void pageRequested(int index);

    private:
        void updateActiveButton(int index);

        QVector<QPushButton *> m_navButtons;
    };
} // Mixed

#endif //MIXEDEDITING_TOPBAR_H
