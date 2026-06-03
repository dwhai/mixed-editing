//
// Created by Anlk on 2026/6/3.
// 封面和视频播放数据模型实现
//

#include "../../include/models/VideoInfo.h"
#include <QJsonArray>

namespace Mixed {
namespace Models {

Cover Cover::fromJson(const QJsonObject& json) {
    Cover cover;
    cover.feed = json["feed"].toString().toStdString();
    cover.detail = json["detail"].toString().toStdString();
    cover.blurred = json["blurred"].toString().toStdString();
    cover.homepage = json["homepage"].toString().toStdString();
    return cover;
}

UrlItem UrlItem::fromJson(const QJsonObject& json) {
    UrlItem urlItem;
    urlItem.name = json["name"].toString().toStdString();
    urlItem.url = json["url"].toString().toStdString();
    urlItem.size = json["size"].toInt();
    return urlItem;
}

PlayInfo PlayInfo::fromJson(const QJsonObject& json) {
    PlayInfo playInfo;
    playInfo.height = json["height"].toInt();
    playInfo.width = json["width"].toInt();
    playInfo.name = json["name"].toString().toStdString();
    playInfo.type = json["type"].toString().toStdString();
    playInfo.url = json["url"].toString().toStdString();
    
    if (json.contains("urlList")) {
        QJsonArray urlListArray = json["urlList"].toArray();
        for (const auto& urlJson : urlListArray) {
            playInfo.urlList.push_back(UrlItem::fromJson(urlJson.toObject()));
        }
    }
    
    return playInfo;
}

WebUrl WebUrl::fromJson(const QJsonObject& json) {
    WebUrl webUrl;
    webUrl.raw = json["raw"].toString().toStdString();
    webUrl.forWeibo = json["forWeibo"].toString().toStdString();
    return webUrl;
}

} // namespace Models
} // namespace Mixed
