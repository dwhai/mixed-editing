//
// Scrollable home content implementation.
//

#include "../include/homepage.h"
#include "../include/components.h"

#include "../../db/include/Database.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace Mixed {

    namespace {
        // 单用户场景固定本地账户 id（与 EditorWindow 一致）。
        const QString kLocalAccount = QStringLiteral("local");

        // 把工程的 updated_at（unix 毫秒）格式化为友好时间串。
        QString formatUpdated(qint64 ms) {
            if (ms <= 0) {
                return QStringLiteral("未保存");
            }
            const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
            return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }

        // 按工程 id 稳定地取一组柔和渐变色（缩略图占位用）。
        // 同一工程每次进入颜色一致，不同工程之间有区分度。
        struct Gradient { QString from; QString to; };
        Gradient gradientForId(const QString &id) {
            static const Gradient palette[] = {
                {QStringLiteral("#5b8def"), QStringLiteral("#6aa6e8")},
                {QStringLiteral("#7b6ef0"), QStringLiteral("#9d7bf0")},
                {QStringLiteral("#3fb5a8"), QStringLiteral("#56c79a")},
                {QStringLiteral("#f0936e"), QStringLiteral("#f0b56e")},
                {QStringLiteral("#ef6e96"), QStringLiteral("#f08fb0")},
                {QStringLiteral("#6e8af0"), QStringLiteral("#6ec7f0")},
            };
            uint hash = 0;
            for (const QChar &c : id) {
                hash = hash * 31u + c.unicode();
            }
            return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
        }
    } // namespace

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
        layout->setSpacing(20);

        layout->addWidget(buildBanner());

        // 「我的工程」标题。
        auto *heading = new QLabel(QStringLiteral("我的工程"), content);
        heading->setProperty("role", "sectionTitle");
        layout->addWidget(heading);

        // 空列表提示。
        m_emptyHint = new QLabel(QStringLiteral("还没有工程，点上方「开始创作」新建一个吧。"),
                                 content);
        m_emptyHint->setObjectName("listStatus");
        layout->addWidget(m_emptyHint);

        // 工程卡片网格。
        m_grid = new QGridLayout();
        m_grid->setContentsMargins(0, 4, 0, 0);
        m_grid->setHorizontalSpacing(18);
        m_grid->setVerticalSpacing(18);
        auto *gridWrap = new QWidget(content);
        gridWrap->setLayout(m_grid);
        layout->addWidget(gridWrap);

        layout->addStretch();

        refreshProjects();
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
        connect(start, &QPushButton::clicked, this, &HomePage::createRequested);

        layout->addStretch();
        layout->addWidget(start);
        layout->addStretch();
        return banner;
    }

    void HomePage::refreshProjects() {
        if (!m_grid) {
            return;
        }
        // 清空现有卡片。
        while (QLayoutItem *item = m_grid->takeAt(0)) {
            if (QWidget *w = item->widget()) {
                w->deleteLater();
            }
            delete item;
        }

        std::vector<DB::Project> projects;
        if (DB::Database::instance().isOpen()) {
            projects = m_projectRepo.listByAccount(kLocalAccount);
        }

        if (m_emptyHint) {
            m_emptyHint->setVisible(projects.empty());
        }

        // 每行 4 张卡片，右侧留一个伸缩列让卡片保持固定宽度并左对齐。
        const int columns = 4;
        for (size_t i = 0; i < projects.size(); ++i) {
            const int row = static_cast<int>(i) / columns;
            const int col = static_cast<int>(i) % columns;
            m_grid->addWidget(buildProjectCard(projects[i]),
                              row, col, Qt::AlignLeft | Qt::AlignTop);
        }
        m_grid->setColumnStretch(columns, 1);
    }

    QWidget *HomePage::buildProjectCard(const DB::Project &project) {
        const QString projectId = project.id;

        auto *card = new QFrame(this);
        card->setObjectName("projectCard");
        card->setCursor(Qt::PointingHandCursor);
        card->setContextMenuPolicy(Qt::CustomContextMenu);

        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // 缩略图区：16:9 渐变色块，中间叠工程名首字大字。无封面时的占位设计。
        auto *thumb = new QLabel(card);
        thumb->setObjectName("projectThumb");
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setFixedHeight(120);
        const QString initial = project.title.isEmpty()
            ? QStringLiteral("?") : project.title.left(1).toUpper();
        thumb->setText(initial);
        const Gradient g = gradientForId(projectId);
        thumb->setStyleSheet(QStringLiteral(
            "QLabel#projectThumb {"
            "  border-top-left-radius: 14px; border-top-right-radius: 14px;"
            "  color: rgba(255,255,255,0.92); font-size: 40px; font-weight: 700;"
            "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
            "      stop:0 %1, stop:1 %2);"
            "}").arg(g.from, g.to));
        layout->addWidget(thumb);

        // 信息区：工程名 + 更新时间。
        auto *info = new QWidget(card);
        info->setObjectName("projectCardInfo");
        auto *infoLayout = new QVBoxLayout(info);
        infoLayout->setContentsMargins(12, 10, 12, 12);
        infoLayout->setSpacing(4);

        auto *title = new QLabel(project.title, info);
        title->setObjectName("projectCardTitle");
        QFontMetrics fm(title->font());
        title->setText(fm.elidedText(project.title, Qt::ElideRight, 168));
        title->setToolTip(project.title);
        infoLayout->addWidget(title);

        auto *meta = new QLabel(QStringLiteral("更新于 ") + formatUpdated(project.meta.updatedAt), info);
        meta->setObjectName("projectCardMeta");
        infoLayout->addWidget(meta);

        layout->addWidget(info);

        // 点击卡片打开工程；右键菜单提供重命名 / 打开。
        card->installEventFilter(this);
        card->setProperty("projectId", projectId);

        connect(card, &QFrame::customContextMenuRequested, this,
                [this, project](const QPoint &pos) {
                    QMenu menu;
                    QAction *rename = menu.addAction(QStringLiteral("重命名"));
                    QAction *open = menu.addAction(QStringLiteral("打开"));
                    QAction *chosen = menu.exec(QCursor::pos());
                    if (chosen == rename) {
                        renameProject(project);
                    } else if (chosen == open) {
                        emit openProjectRequested(project.id);
                    }
                    Q_UNUSED(pos);
                });

        return card;
    }

    void HomePage::renameProject(const DB::Project &project) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("重命名工程"), QStringLiteral("工程名称："),
            QLineEdit::Normal, project.title, &ok);
        if (!ok) {
            return;
        }
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty() || trimmed == project.title) {
            return;
        }
        DB::Project updated = project;
        updated.title = trimmed;
        if (m_projectRepo.update(updated)) {
            refreshProjects();
        }
    }

    bool HomePage::eventFilter(QObject *watched, QEvent *event) {
        // 卡片左键单击 → 打开对应工程。
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                if (auto *w = qobject_cast<QWidget *>(watched)) {
                    const QString id = w->property("projectId").toString();
                    if (!id.isEmpty()) {
                        emit openProjectRequested(id);
                        return true;
                    }
                }
            }
        }
        return QWidget::eventFilter(watched, event);
    }
} // Mixed
