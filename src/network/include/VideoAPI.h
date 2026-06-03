//
// Created by Anlk on 2026/6/3.
// 网络API接口
//

#ifndef MIXEDEDITING_API_H
#define MIXEDEDITING_API_H

#include "models/Responses.h"
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <functional>

namespace Mixed {
namespace API {

// API回调类型
using FeedCallback = std::function<void(const Models::FeedResponse&, bool success)>;
using CategoryCallback = std::function<void(const std::vector<Models::Category>&, bool success)>;
using TopicCallback = std::function<void(const Models::TopicResponse&, bool success)>;
using RankCallback = std::function<void(const Models::RankResponse&, bool success)>;
using ErrorCallback = std::function<void(const QString& errorMessage)>;

// 视频API接口
class VideoAPI {
public:
    explicit VideoAPI(QNetworkAccessManager* manager);
    
    // 获取首页数据（date 为毫秒时间戳；传 0 时服务端返回空列表）
    void getFeed(qint64 date, FeedCallback callback, ErrorCallback errorCallback = nullptr);
    
    // 获取分类数据
    void getCategories(int start, CategoryCallback callback, ErrorCallback errorCallback = nullptr);
    
    // 获取专题接口
    void getSpecialTopics(int start, TopicCallback callback, ErrorCallback errorCallback = nullptr);
    
    // 获取排行榜
    void getRankList(RankCallback callback, ErrorCallback errorCallback = nullptr, const QString& strategy = "weekly");
    
    // 获取视频详情
    void getVideoDetail(int videoId, FeedCallback callback, ErrorCallback errorCallback = nullptr);

private:
    QNetworkAccessManager* networkManager;
    const QString baseUrl = "https://baobab.kaiyanapp.com";
    
    // 通用GET请求
    void getRequest(const QString& url, const std::function<void(QNetworkReply*)>& successHandler, ErrorCallback errorCallback = nullptr);
};

} // namespace API
} // namespace Mixed

#endif //MIXEDEDITING_API_H
