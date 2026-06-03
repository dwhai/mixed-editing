//
// Created by Anlk on 2026/6/3.
// 标签数据模型实现
//

#include "../../include/models/Tag.h"

namespace Mixed {
namespace Models {

Tag Tag::fromJson(const QJsonObject& json) {
    Tag tag;
    tag.id = json["id"].toInt();
    tag.name = json["name"].toString().toStdString();
    tag.actionUrl = json["actionUrl"].toString().toStdString();
    tag.desc = json["desc"].toString().toStdString();
    tag.bgPicture = json["bgPicture"].toString().toStdString();
    tag.headerImage = json["headerImage"].toString().toStdString();
    tag.tagRecType = json["tagRecType"].toString().toStdString();
    tag.haveReward = json["haveReward"].toBool();
    tag.ifNewest = json["ifNewest"].toBool();
    tag.communityIndex = json["communityIndex"].toInt();
    return tag;
}

} // namespace Models
} // namespace Mixed
