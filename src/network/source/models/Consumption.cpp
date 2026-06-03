//
// Created by Anlk on 2026/6/3.
// 消费数据模型实现
//

#include "../../include/models/Consumption.h"

namespace Mixed {
namespace Models {

Consumption Consumption::fromJson(const QJsonObject& json) {
    Consumption consumption;
    consumption.collectionCount = json["collectionCount"].toInt();
    consumption.shareCount = json["shareCount"].toInt();
    consumption.replyCount = json["replyCount"].toInt();
    consumption.realCollectionCount = json["realCollectionCount"].toInt();
    return consumption;
}

} // namespace Models
} // namespace Mixed
