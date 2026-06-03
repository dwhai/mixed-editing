//
// Left navigation sidebar implementation.
//

#include "../include/sidebar.h"
#include "../include/components.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace Mixed {
    namespace {
        // Navigation entries shown in the sidebar. The first one starts active.
        const QStringList kNavItems = {
                QStringLiteral("首页"),
                QStringLiteral("模板"),
                QStringLiteral("短剧"),
        };
    }

    Sidebar::Sidebar(QWidget *parent) : QFrame(parent) {
        setObjectName("sidebar");
        setFixedWidth(210);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(10);

        layout->addWidget(buildAccountCard());

        for (int i = 0; i < kNavItems.size(); ++i) {
            layout->addWidget(Components::navButton(kNavItems.at(i), /*active=*/i == 0, this));
        }

        layout->addStretch();
        layout->addWidget(buildDiscoverCard());
    }

    QWidget *Sidebar::buildAccountCard() {
        auto *card = new QFrame(this);
        card->setObjectName("accountCard");

        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);

        auto *label = new QLabel(QStringLiteral("点击登录账户"), card);
        label->setObjectName("accountLabel");

        layout->addWidget(label);
        return card;
    }

    QWidget *Sidebar::buildDiscoverCard() {
        auto *card = new QFrame(this);
        card->setObjectName("discoverCard");

        auto *layout = new QHBoxLayout(card);
        layout->setContentsMargins(10, 9, 10, 9);
        return card;
    }
} // Mixed
