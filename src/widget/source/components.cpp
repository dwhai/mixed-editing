//
// Implementation of the reusable UI building blocks.
//

#include "../include/components.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace Mixed::Components {
    QPushButton *navButton(const QString &text, bool active, QWidget *parent) {
        auto *button = new QPushButton(text, parent);
        button->setProperty("role", active ? "navActive" : "nav");
        button->setCursor(Qt::PointingHandCursor);
        return button;
    }
}
