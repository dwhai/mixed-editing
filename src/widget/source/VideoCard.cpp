//
// VideoCard implementation.
//

#include "../include/VideoCard.h"

#include <QDebug>
#include <QImage>
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QPixmapCache>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <webp/decode.h>

namespace Mixed {

    namespace {
        // 将下载到的图片字节解码为 QPixmap。
        // 优先用 Qt 自带解码器（jpeg/png/gif…）；当本机 Qt 缺少 webp 图片插件而
        // 开眼 CDN 又统一返回 webp 时，回退到 libwebp 解码。
        QPixmap decodeImage(const QByteArray &data) {
            QPixmap pix;
            if (pix.loadFromData(data)) return pix;

            // WebP 魔数：'RIFF' .... 'WEBP'
            if (data.size() >= 12 &&
                data.startsWith("RIFF") &&
                data.mid(8, 4) == QByteArray("WEBP")) {
                int w = 0, h = 0;
                const auto *bytes = reinterpret_cast<const uint8_t *>(data.constData());
                if (WebPGetInfo(bytes, static_cast<size_t>(data.size()), &w, &h) &&
                    w > 0 && h > 0) {
                    uint8_t *rgba = WebPDecodeRGBA(bytes, static_cast<size_t>(data.size()),
                                                   &w, &h);
                    if (rgba) {
                        // QImage 需要持续持有像素数据：用拷贝构造一份独立副本后释放原始缓冲。
                        QImage img(rgba, w, h, w * 4, QImage::Format_RGBA8888);
                        QPixmap result = QPixmap::fromImage(img.copy());
                        WebPFree(rgba);
                        return result;
                    }
                }
            }
            return QPixmap(); // 解码失败返回空
        }
    }

    VideoCard::VideoCard(const Models::VideoData &video, int index,
                         QNetworkAccessManager *network, QWidget *parent)
        : QFrame(parent), m_index(index), m_network(network) {
        setObjectName("videoCard");
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);

        // 缩略图（16:9）。承载时长角标。
        m_thumb = new QLabel(this);
        m_thumb->setObjectName("cardThumb");
        m_thumb->setFixedHeight(150);
        m_thumb->setAlignment(Qt::AlignCenter);
        m_thumb->setScaledContents(false);
        // 时长角标叠在缩略图右下角（用一个撑满的布局把它推到右下）。
        auto *duration = new QLabel(formatDuration(video.duration), m_thumb);
        duration->setObjectName("cardDuration");
        auto *badgeLayout = new QVBoxLayout(m_thumb);
        badgeLayout->setContentsMargins(8, 8, 8, 8);
        badgeLayout->addStretch();
        auto *badgeRow = new QHBoxLayout();
        badgeRow->addStretch();
        badgeRow->addWidget(duration);
        badgeLayout->addLayout(badgeRow);

        layout->addWidget(m_thumb);

        // 标题（最多两行）。
        QString title = QString::fromStdString(video.title);
        if (title.isEmpty()) title = QStringLiteral("(无标题)");
        auto *titleLabel = new QLabel(title, this);
        titleLabel->setObjectName("cardTitle");
        titleLabel->setWordWrap(true);
        titleLabel->setFixedHeight(40);
        layout->addWidget(titleLabel);

        // 作者名。
        QString author = QString::fromStdString(video.author.name);
        if (author.isEmpty()) author = QString::fromStdString(video.category);
        auto *authorLabel = new QLabel(author, this);
        authorLabel->setObjectName("cardAuthor");
        layout->addWidget(authorLabel);

        // 优先使用 detail 封面（列表项主封面），回退到 feed。
        // 打印 cover 结构体各字段，便于排查“数据为空”还是“显示问题”。
        qInfo().noquote() << QStringLiteral("[VideoCard #%1] cover{")
                                 .arg(index)
                          << "detail=" << QString::fromStdString(video.cover.detail)
                          << "| feed=" << QString::fromStdString(video.cover.feed)
                          << "| blurred=" << QString::fromStdString(video.cover.blurred)
                          << "| homepage=" << QString::fromStdString(video.cover.homepage)
                          << "}";

        QString cover = QString::fromStdString(video.cover.detail);
        if (cover.isEmpty()) cover = QString::fromStdString(video.cover.feed);
        if (!cover.isEmpty()) {
            loadThumbnail(cover);
        } else {
            qWarning().noquote() << QStringLiteral("[VideoCard #%1] 封面字段全部为空，无图可加载")
                                        .arg(index);
        }
    }

    VideoCard::~VideoCard() {
        // 卡片在翻页/重排时会被销毁重建；若封面请求仍在进行，必须中止并释放，
        // 否则 reply 作为僵尸长期占用 QNAM 的每主机连接（默认 6 个），
        // 导致后续封面请求排队不返回，表现为大量卡片封面常黑。
        if (m_reply) {
            // 关键：先断开信号再 abort。abort() 会同步发出 finished()，
            // 若不先断开，lambda 会在析构中途运行并把 m_reply 置空，
            // 随后这里再 deleteLater() 就会解引用空指针而崩溃。
            m_reply->disconnect();
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }
    }

    void VideoCard::loadThumbnail(const QString &url) {
        if (!m_network) return;

        // 命中进程级缓存则直接使用，避免重排/翻页时重复下载（也是消除连接抖动的关键）。
        QPixmap cached;
        if (QPixmapCache::find(url, &cached)) {
            // qInfo().noquote() << QStringLiteral("[VideoCard #%1] 命中缓存 %2")
            //                          .arg(m_index).arg(url);
            m_source = cached;
            updateThumbnail();
            return;
        }

        // qInfo().noquote() << QStringLiteral("[VideoCard #%1] 发起封面请求 %2")
        //                          .arg(m_index).arg(url);
        m_reply = m_network->get(QNetworkRequest(QUrl(url)));
        connect(m_reply, &QNetworkReply::finished, this, [this, url]() {
            QNetworkReply *reply = m_reply;
            m_reply = nullptr;
            if (!reply) return;
            reply->deleteLater();

            const int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning().noquote()
                    << QStringLiteral("[VideoCard #%1] 封面请求失败 status=%2 err=%3 url=%4")
                           .arg(m_index).arg(status)
                           .arg(reply->errorString(), url);
                return;
            }

            const QByteArray data = reply->readAll();
            QPixmap pix = decodeImage(data);
            if (!pix.isNull()) {
                qInfo().noquote()
                    << QStringLiteral("[VideoCard #%1] 封面加载成功 status=%2 bytes=%3 size=%4x%5")
                           .arg(m_index).arg(status).arg(data.size())
                           .arg(pix.width()).arg(pix.height());
                QPixmapCache::insert(url, pix);
                m_source = pix;
                updateThumbnail();
            } else {
                // 数据非空但无法解码：多为格式不支持或返回了非图片内容。
                qWarning().noquote()
                    << QStringLiteral("[VideoCard #%1] 封面解码失败 status=%2 bytes=%3 head=%4 url=%5")
                           .arg(m_index).arg(status).arg(data.size())
                           .arg(QString::fromLatin1(data.left(16).toHex(' ')), url);
            }
        });
    }

    void VideoCard::updateThumbnail() {
        if (m_source.isNull() || !m_thumb) return;
        QSize target = m_thumb->size();
        // 控件尚未完成首次布局时宽度可能为 0，用固定高度按 16:9 兜底，
        // 避免缩放成空图导致封面不显示。
        if (target.width() <= 0) target.setWidth(target.height() * 16 / 9);
        if (target.width() <= 0 || target.height() <= 0) return;

        const qreal dpr = devicePixelRatioF();
        const QSize devTarget = target * dpr;
        // 等比放大铺满后居中裁剪到目标尺寸。
        QPixmap scaled = m_source.scaled(devTarget, Qt::KeepAspectRatioByExpanding,
                                         Qt::SmoothTransformation);
        const int x = (scaled.width() - devTarget.width()) / 2;
        const int y = (scaled.height() - devTarget.height()) / 2;
        QPixmap cropped = scaled.copy(x, y, devTarget.width(), devTarget.height());
        cropped.setDevicePixelRatio(dpr);
        m_thumb->setPixmap(cropped);
    }

    void VideoCard::resizeEvent(QResizeEvent *event) {
        QFrame::resizeEvent(event);
        updateThumbnail();
    }

    void VideoCard::mousePressEvent(QMouseEvent *event) {
        if (event->button() == Qt::LeftButton) {
            emit clicked(m_index);
        }
        QFrame::mousePressEvent(event);
    }

    QString VideoCard::formatDuration(int seconds) {
        if (seconds < 0) seconds = 0;
        const int m = seconds / 60;
        const int s = seconds % 60;
        return QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    }
} // Mixed
