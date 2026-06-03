//
// Created by Anlk on 2026/6/3.
// PlayerPage 实现。
//

#include "../include/PlayerPage.h"
#include "../include/VideoGLWidget.h"

#include "../../ffmpeg/include/MediaPlayer.h"
#include "../../network/include/VideoAPI.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QVBoxLayout>

namespace Mixed::Player {

    PlayerPage::PlayerPage(QWidget *parent) : QWidget(parent) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);

        // 视频显示控件（硬件加速）。
        m_video = new VideoGLWidget(this);
        m_video->setMinimumSize(480, 270);
        layout->addWidget(m_video, /*stretch=*/1);

        // 控制条。
        auto *bar = new QHBoxLayout();
        bar->setContentsMargins(4, 0, 4, 0);

        m_status = new QLabel(QStringLiteral("正在获取视频列表..."), this);
        m_status->setObjectName("playerStatus");
        bar->addWidget(m_status, /*stretch=*/1);

        m_nextButton = new QPushButton(QStringLiteral("下一个"), this);
        m_nextButton->setObjectName("playerNextButton");
        m_nextButton->setCursor(Qt::PointingHandCursor);
        m_nextButton->setEnabled(false);
        bar->addWidget(m_nextButton);
        layout->addLayout(bar);

        connect(m_nextButton, &QPushButton::clicked, this, [this] {
            if (m_urls.empty()) return;
            playIndex((m_current + 1) % static_cast<int>(m_urls.size()));
        });

        m_player = new MediaPlayer(this);
        // 解码引擎与显示控件通过信号解耦：引擎出帧，控件上屏。
        connect(m_player, &MediaPlayer::frameReady, this, [this](const VideoFrame &f) {
            m_video->setFrame(f);
        });
        connect(m_player, &MediaPlayer::cleared, this, [this] {
            m_video->clearFrame();
        });
        connect(m_player, &MediaPlayer::errorOccurred, this, [this](const QString &msg) {
            m_status->setText(QStringLiteral("播放错误: ") + msg);
        });

        m_network = new QNetworkAccessManager(this);
        m_api = new API::VideoAPI(m_network);

        fetchPlaylist();
    }

    PlayerPage::~PlayerPage() {
        if (m_player) m_player->stop();
        delete m_api;
    }

    void PlayerPage::fetchPlaylist() {
        // 注意：date=0 时开眼接口返回空列表，必须传入真实的毫秒时间戳。
        m_api->getFeed(QDateTime::currentMSecsSinceEpoch(),
            [this](const Models::FeedResponse &response, bool success) {
                if (!success) {
                    m_status->setText(QStringLiteral("获取列表失败"));
                    return;
                }
                m_urls.clear();
                m_titles.clear();
                for (const auto &issue : response.issueList) {
                    for (const auto &item : issue.itemList) {
                        const auto &data = item.data;
                        if (!data.playUrl.empty() && !data.ad) {
                            m_urls.emplace_back(QString::fromStdString(data.playUrl));
                            m_titles.emplace_back(QString::fromStdString(data.title));
                        }
                    }
                }

                if (m_urls.empty()) {
                    m_status->setText(QStringLiteral("没有可播放的视频"));
                    return;
                }
                m_nextButton->setEnabled(true);
                qDebug() << "获取到" << m_urls.size() << "个视频 url:" << m_urls[0];
                m_status->setText(QStringLiteral("共获取到 %1 个视频").arg(m_urls.size()));
                playIndex(0);
            },
            [this](const QString &err) {
                m_status->setText(QStringLiteral("网络错误: ") + err);
            });
    }

    void PlayerPage::playIndex(int index) {
        if (index < 0 || index >= static_cast<int>(m_urls.size())) return;
        m_current = index;
        const QString title = m_titles[index].isEmpty()
                              ? QStringLiteral("(无标题)")
                              : m_titles[index];
        m_status->setText(QStringLiteral("正在播放 [%1/%2] %3")
                              .arg(index + 1)
                              .arg(m_urls.size())
                              .arg(title));
        m_player->play(m_urls[index]);
    }

} // namespace Mixed::Player
