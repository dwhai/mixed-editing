//
// Scrollable home content implementation.
//

#include "../include/homepage.h"
#include "../include/components.h"

#include "../../db/include/Database.h"

#include <QDateTime>
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
        m_grid->setContentsMargins(0, 0, 0, 0);
        m_grid->setHorizontalSpacing(16);
        m_grid->setVerticalSpacing(16);
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

        // 每行 4 张卡片。
        const int columns = 4;
        for (size_t i = 0; i < projects.size(); ++i) {
            const int row = static_cast<int>(i) / columns;
            const int col = static_cast<int>(i) % columns;
            m_grid->addWidget(buildProjectCard(projects[i]),
                              row, col, Qt::AlignLeft | Qt::AlignTop);
        }
    }

    QWidget *HomePage::buildProjectCard(const DB::Project &project) {
        const QString projectId = project.id;

        auto *card = new QFrame(this);
        card->setObjectName("draftCard");
        card->setCursor(Qt::PointingHandCursor);
        card->setContextMenuPolicy(Qt::CustomContextMenu);

        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(8);

        // 缩略图占位（无封面时显示工程名首字）。
        auto *thumb = new QLabel(card);
        thumb->setObjectName("draftThumb");
        thumb->setAlignment(Qt::AlignCenter);
        const QString initial = project.title.isEmpty()
            ? QStringLiteral("?") : project.title.left(1);
        thumb->setText(initial);
        layout->addWidget(thumb, 0, Qt::AlignHCenter);

        auto *title = new QLabel(project.title, card);
        title->setObjectName("draftDate");
        title->setWordWrap(true);
        layout->addWidget(title);

        auto *info = new QLabel(formatUpdated(project.meta.updatedAt), card);
        info->setObjectName("draftInfo");
        layout->addWidget(info);

        // 点击卡片打开工程；右键菜单提供重命名。
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
