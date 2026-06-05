//
// Left navigation sidebar implementation.
//

#include "../include/sidebar.h"
#include "../include/components.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
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
            auto *btn = Components::navButton(kNavItems.at(i), /*active=*/i == 0, this);
            m_navButtons.append(btn);
            connect(btn, &QPushButton::clicked, this, [this, i]() {
                updateActiveButton(i);
                emit pageRequested(i);
            });
            layout->addWidget(btn);
        }

        layout->addStretch();
        layout->addWidget(buildDiscoverCard());
    }

    void Sidebar::updateActiveButton(int index) {
        for (int i = 0; i < m_navButtons.size(); ++i) {
            m_navButtons[i]->setProperty("role", i == index ? "navActive" : "nav");
            m_navButtons[i]->style()->unpolish(m_navButtons[i]);
            m_navButtons[i]->style()->polish(m_navButtons[i]);
        }
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
