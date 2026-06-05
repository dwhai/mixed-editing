//
// Top navigation bar implementation.
//

#include "../include/topbar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>

namespace Mixed {
    namespace {
        // Navigation entries shown in the top bar. The first one starts active.
        const QStringList kNavItems = {
            QStringLiteral("首页"),
            QStringLiteral("剪辑"),
            QStringLiteral("模版"),
        };
    }

    TopBar::TopBar(QWidget *parent) : QFrame(parent) {
        setObjectName("topBar");
        setFixedHeight(56);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(16, 8, 16, 8);
        layout->setSpacing(6);

        // 左侧：横向 tab 菜单。
        for (int i = 0; i < kNavItems.size(); ++i) {
            auto *btn = new QPushButton(kNavItems.at(i), this);
            btn->setProperty("role", i == 0 ? "topNavActive" : "topNav");
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFocusPolicy(Qt::NoFocus);
            m_navButtons.append(btn);
            connect(btn, &QPushButton::clicked, this, [this, i]() {
                updateActiveButton(i);
                emit pageRequested(i);
            });
            layout->addWidget(btn);
        }

        layout->addStretch();

        // 右侧：账户入口。
        auto *account = new QPushButton(QStringLiteral("点击登录账户"), this);
        account->setObjectName("accountButton");
        account->setCursor(Qt::PointingHandCursor);
        account->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(account);
    }

    void TopBar::updateActiveButton(int index) {
        for (int i = 0; i < m_navButtons.size(); ++i) {
            m_navButtons[i]->setProperty("role", i == index ? "topNavActive" : "topNav");
            m_navButtons[i]->style()->unpolish(m_navButtons[i]);
            m_navButtons[i]->style()->polish(m_navButtons[i]);
        }
    }
} // Mixed
