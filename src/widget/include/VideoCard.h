//
// A single video card in the 短剧 grid: 16:9 thumbnail with a duration badge,
// title and author. Thumbnails are fetched asynchronously over the network.
//

#ifndef MIXEDEDITING_VIDEOCARD_H
#define MIXEDEDITING_VIDEOCARD_H

#include <QFrame>
#include <QPixmap>
#include "../../network/include/models/VideoData.h"

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

namespace Mixed {
    class VideoCard : public QFrame {
        Q_OBJECT

    public:
        // index 为该卡片在列表中的位置，点击时回传，便于打开播放器时定位。
        VideoCard(const Models::VideoData &video, int index,
                  QNetworkAccessManager *network, QWidget *parent = nullptr);
        ~VideoCard() override;

    signals:
        void clicked(int index);

    protected:
        void mousePressEvent(QMouseEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;

    private:
        void loadThumbnail(const QString &url);
        void updateThumbnail(); // 按当前缩略图尺寸等比裁剪源图并显示
        static QString formatDuration(int seconds);

        int m_index;
        QNetworkAccessManager *m_network;
        QLabel *m_thumb = nullptr;
        QPixmap m_source;             // 原始封面，随控件尺寸变化重新缩放
        QNetworkReply *m_reply = nullptr; // 进行中的封面请求，析构时中止以释放连接
    };
} // Mixed

#endif //MIXEDEDITING_VIDEOCARD_H
