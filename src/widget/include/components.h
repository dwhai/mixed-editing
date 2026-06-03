//
// Small, reusable UI building blocks.
//
// These factory helpers return ready-to-use widgets that pick up their styling
// from the global theme (see theme.h). Use them anywhere a nav button, a circular
// function entry, or an "upgrade" card is needed.
//

#ifndef MIXEDEDITING_COMPONENTS_H
#define MIXEDEDITING_COMPONENTS_H

#include <QString>

class QFrame;
class QPushButton;
class QWidget;

namespace Mixed::Components {
    // A left-aligned sidebar navigation button. When active it uses the
    // highlighted style.
    QPushButton *navButton(const QString &text, bool active = false, QWidget *parent = nullptr);

}

#endif //MIXEDEDITING_COMPONENTS_H
