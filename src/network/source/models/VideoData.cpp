//
// Created by Anlk on 2026/6/3.
// 视频数据模型实现
//

#include "../../include/models/VideoData.h"
#include <QJsonArray>

namespace Mixed {
namespace Models {

VideoData VideoData::fromJson(const QJsonObject& json) {
    VideoData video;
    video.dataType = json["dataType"].toString().toStdString();
    video.id = json["id"].toInt();
    video.title = json["title"].toString().toStdString();
    video.description = json["description"].toString().toStdString();
    video.library = json["library"].toString().toStdString();
    video.resourceType = json["resourceType"].toString().toStdString();
    video.slogan = json["slogan"].toString().toStdString();
    video.category = json["category"].toString().toStdString();
    video.playUrl = json["playUrl"].toString().toStdString();
    video.thumbPlayUrl = json["thumbPlayUrl"].toString().toStdString();
    video.duration = json["duration"].toInt();
    video.releaseTime = json["releaseTime"].toVariant().toLongLong();
    video.ad = json["ad"].toBool();
    video.type = json["type"].toString().toStdString();
    video.descriptionEditor = json["descriptionEditor"].toString().toStdString();
    video.collected = json["collected"].toBool();
    video.reallyCollected = json["reallyCollected"].toBool();
    video.played = json["played"].toBool();
    
    if (json.contains("tags")) {
        QJsonArray tagsArray = json["tags"].toArray();
        for (const auto& tagJson : tagsArray) {
            video.tags.push_back(Tag::fromJson(tagJson.toObject()));
        }
    }
    
    if (json.contains("consumption")) {
        video.consumption = Consumption::fromJson(json["consumption"].toObject());
    }
    
    if (json.contains("provider")) {
        video.provider = Provider::fromJson(json["provider"].toObject());
    }
    
    if (json.contains("author")) {
        video.author = Author::fromJson(json["author"].toObject());
    }
    
    if (json.contains("cover")) {
        video.cover = Cover::fromJson(json["cover"].toObject());
    }
    
    if (json.contains("webUrl")) {
        video.webUrl = WebUrl::fromJson(json["webUrl"].toObject());
    }
    
    if (json.contains("playInfo")) {
        QJsonArray playInfoArray = json["playInfo"].toArray();
        for (const auto& playInfoJson : playInfoArray) {
            video.playInfo.push_back(PlayInfo::fromJson(playInfoJson.toObject()));
        }
    }
    
    return video;
}

} // namespace Models
} // namespace Mixed
