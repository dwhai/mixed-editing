//
// Scrollable home content: banner + 工程列表（卡片网格）。
// 点卡片打开对应工程，「开始创作」新建空工程；返回主窗口时刷新列表。
//

#ifndef MIXEDEDITING_HOMEPAGE_H
#define MIXEDEDITING_HOMEPAGE_H

#include <QWidget>

#include "../../db/include/Repositories.h"

class QGridLayout;
class QLabel;

namespace Mixed {
    class HomePage : public QWidget {
        Q_OBJECT

    public:
        explicit HomePage(QWidget *parent = nullptr);

        // 重新从数据库加载工程并重绘卡片网格（进入/返回首页时调用）。
        void refreshProjects();

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override;

    signals:
        // 「开始创作」被点击，请求新建工程并打开剪辑工作区。
        void createRequested();
        // 点击某工程卡片，请求打开该工程。
        void openProjectRequested(const QString &projectId);

    private:
        QWidget *buildBanner();
        QWidget *buildProjectCard(const DB::Project &project);
        void renameProject(const DB::Project &project);

        DB::ProjectRepository m_projectRepo;
        QGridLayout *m_grid = nullptr;   // 工程卡片网格
        QLabel      *m_emptyHint = nullptr;
    };
} // Mixed

#endif //MIXEDEDITING_HOMEPAGE_H
