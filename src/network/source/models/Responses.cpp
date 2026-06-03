//
// Created by Anlk on 2026/6/3.
// 响应数据模型实现
//

#include "../../include/models/Responses.h"
#include <QJsonArray>

namespace Mixed {
namespace Models {

ListItem ListItem::fromJson(const QJsonObject& json) {
    ListItem item;
    item.type = json["type"].toString().toStdString();
    item.id = json["id"].toInt();
    item.adIndex = json["adIndex"].toInt();
    
    if (json.contains("data")) {
        item.data = VideoData::fromJson(json["data"].toObject());
    }
    
    return item;
}

IssueItem IssueItem::fromJson(const QJsonObject& json) {
    IssueItem issue;
    issue.releaseTime = json["releaseTime"].toVariant().toLongLong();
    issue.type = json["type"].toString().toStdString();
    issue.date = json["date"].toInt();
    issue.publishTime = json["publishTime"].toVariant().toLongLong();
    issue.count = json["count"].toInt();
    
    if (json.contains("itemList")) {
        QJsonArray itemArray = json["itemList"].toArray();
        for (const auto& itemJson : itemArray) {
            issue.itemList.push_back(ListItem::fromJson(itemJson.toObject()));
        }
    }
    
    return issue;
}

FeedResponse FeedResponse::fromJson(const QJsonObject& json) {
    FeedResponse response;
    
    if (json.contains("issueList")) {
        QJsonArray issueArray = json["issueList"].toArray();
        for (const auto& issueJson : issueArray) {
            response.issueList.push_back(IssueItem::fromJson(issueJson.toObject()));
        }
    }
    
    response.nextPageUrl = json["nextPageUrl"].toString().toStdString();
    response.nextPublishTime = json["nextPublishTime"].toVariant().toLongLong();
    response.newestIssueType = json["newestIssueType"].toString().toStdString();
    
    return response;
}

Category Category::fromJson(const QJsonObject& json) {
    Category category;
    category.id = json["id"].toInt();
    category.name = json["name"].toString().toStdString();
    category.description = json["description"].toString().toStdString();
    category.bgPicture = json["bgPicture"].toString().toStdString();
    category.bgColor = json["bgColor"].toString().toStdString();
    category.headerImage = json["headerImage"].toString().toStdString();
    category.defaultAuthorId = json["defaultAuthorId"].toInt();
    category.tagId = json["tagId"].toInt();
    return category;
}

Label Label::fromJson(const QJsonObject& json) {
    Label label;
    label.text = json["text"].toString().toStdString();
    label.card = json["card"].toString().toStdString();
    return label;
}

TopicItem TopicItem::fromJson(const QJsonObject& json) {
    TopicItem item;
    item.type = json["type"].toString().toStdString();
    item.id = json["id"].toInt();
    item.adIndex = json["adIndex"].toInt();
    
    if (json.contains("data")) {
        QJsonObject dataObj = json["data"].toObject();
        item.dataType = dataObj["dataType"].toString().toStdString();
        item.title = dataObj["title"].toString().toStdString();
        item.description = dataObj["description"].toString().toStdString();
        item.image = dataObj["image"].toString().toStdString();
        item.actionUrl = dataObj["actionUrl"].toString().toStdString();
        item.shade = dataObj["shade"].toBool();
        item.autoPlay = dataObj["autoPlay"].toBool();
        
        if (dataObj.contains("label")) {
            item.label = Label::fromJson(dataObj["label"].toObject());
        }
    }
    
    return item;
}

TopicResponse TopicResponse::fromJson(const QJsonObject& json) {
    TopicResponse response;
    response.count = json["count"].toInt();
    response.total = json["total"].toInt();
    response.nextPageUrl = json["nextPageUrl"].toString().toStdString();
    response.adExist = json["adExist"].toBool();
    
    if (json.contains("itemList")) {
        QJsonArray itemArray = json["itemList"].toArray();
        for (const auto& itemJson : itemArray) {
            response.itemList.push_back(TopicItem::fromJson(itemJson.toObject()));
        }
    }
    
    return response;
}

RankResponse RankResponse::fromJson(const QJsonObject& json) {
    RankResponse response;
    response.count = json["count"].toInt();
    response.total = json["total"].toInt();
    response.nextPageUrl = json["nextPageUrl"].toString().toStdString();
    response.adExist = json["adExist"].toBool();
    
    if (json.contains("itemList")) {
        QJsonArray itemArray = json["itemList"].toArray();
        for (const auto& itemJson : itemArray) {
            response.itemList.push_back(ListItem::fromJson(itemJson.toObject()));
        }
    }
    
    return response;
}

} // namespace Models
} // namespace Mixed
