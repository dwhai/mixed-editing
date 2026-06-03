//
// Created by Anlk on 2026/6/3.
// 提供者和作者数据模型实现
//

#include "../../include/models/Provider.h"
#include "../../include/models/Author.h"

namespace Mixed {
namespace Models {

Provider Provider::fromJson(const QJsonObject& json) {
    Provider provider;
    provider.name = json["name"].toString().toStdString();
    provider.alias = json["alias"].toString().toStdString();
    provider.icon = json["icon"].toString().toStdString();
    return provider;
}

Follow Follow::fromJson(const QJsonObject& json) {
    Follow follow;
    follow.itemType = json["itemType"].toString().toStdString();
    follow.itemId = json["itemId"].toInt();
    follow.followed = json["followed"].toBool();
    return follow;
}

Shield Shield::fromJson(const QJsonObject& json) {
    Shield shield;
    shield.itemType = json["itemType"].toString().toStdString();
    shield.itemId = json["itemId"].toInt();
    shield.shielded = json["shielded"].toBool();
    return shield;
}

Author Author::fromJson(const QJsonObject& json) {
    Author author;
    author.id = json["id"].toInt();
    author.icon = json["icon"].toString().toStdString();
    author.name = json["name"].toString().toStdString();
    author.description = json["description"].toString().toStdString();
    author.link = json["link"].toString().toStdString();
    author.latestReleaseTime = json["latestReleaseTime"].toVariant().toLongLong();
    author.videoNum = json["videoNum"].toInt();
    author.ifPgc = json["ifPgc"].toBool();
    
    if (json.contains("follow")) {
        author.follow = Follow::fromJson(json["follow"].toObject());
    }
    
    if (json.contains("shield")) {
        author.shield = Shield::fromJson(json["shield"].toObject());
    }
    
    return author;
}

} // namespace Models
} // namespace Mixed
