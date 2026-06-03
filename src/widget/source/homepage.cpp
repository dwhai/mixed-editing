//
// Scrollable home content implementation.
//

#include "../include/homepage.h"
#include "../include/components.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace Mixed {

    HomePage::HomePage(QWidget *parent) : QWidget(parent) {
        auto *outerLayout = new QVBoxLayout(this);
        outerLayout->setContentsMargins(0, 0, 0, 0);

        auto *scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        outerLayout->addWidget(scrollArea);

        auto *content = new QWidget(scrollArea);
        content->setObjectName("scrollContent");
        scrollArea->setWidget(content);

        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(10, 8, 8, 12);
        layout->setSpacing(24);

        layout->addWidget(buildBanner());
        layout->addStretch();
    }

    QWidget *HomePage::buildBanner() {
        auto *banner = new QFrame(this);
        banner->setObjectName("topBanner");
        banner->setMinimumHeight(108);

        auto *layout = new QHBoxLayout(banner);
        layout->setContentsMargins(20, 18, 20, 18);

        auto *start = new QPushButton(QStringLiteral("+  开始创作"), banner);
        start->setObjectName("startButton");
        start->setCursor(Qt::PointingHandCursor);

        layout->addStretch();
        layout->addWidget(start);
        layout->addStretch();
        return banner;
    }
} // Mixed
