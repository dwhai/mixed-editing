//
// Created by Anlk on 2026/6/3.
// 网络API实现
//

#include "../include/VideoAPI.h"
#include "../include/models/Responses.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace Mixed {
namespace API {

VideoAPI::VideoAPI(QNetworkAccessManager* manager) : networkManager(manager) {
}

void VideoAPI::getFeed(qint64 date, FeedCallback callback, ErrorCallback errorCallback) {
    QString url = QString("%1/api/v2/feed?date=%2").arg(baseUrl).arg(date);
    
    getRequest(url, [callback](QNetworkReply* reply) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject json = doc.object();
        
        Models::FeedResponse response;
        
        if (json.contains("issueList")) {
            QJsonArray issueArray = json["issueList"].toArray();
            for (const auto& issueJson : issueArray) {
                Models::IssueItem issue;
                QJsonObject issueObj = issueJson.toObject();
                
                issue.releaseTime = issueObj["releaseTime"].toVariant().toLongLong();
                issue.type = issueObj["type"].toString().toStdString();
                issue.date = issueObj["date"].toInt();
                issue.publishTime = issueObj["publishTime"].toVariant().toLongLong();
                issue.count = issueObj["count"].toInt();
                
                if (issueObj.contains("itemList")) {
                    QJsonArray itemArray = issueObj["itemList"].toArray();
                    for (const auto& itemJson : itemArray) {
                        issue.itemList.push_back(Models::ListItem::fromJson(itemJson.toObject()));
                    }
                }
                
                response.issueList.push_back(issue);
            }
        }
        
        response.nextPageUrl = json["nextPageUrl"].toString().toStdString();
        response.nextPublishTime = json["nextPublishTime"].toVariant().toLongLong();
        response.newestIssueType = json["newestIssueType"].toString().toStdString();
        
        callback(response, true);
    }, errorCallback);
}

void VideoAPI::getCategories(int start, CategoryCallback callback, ErrorCallback errorCallback) {
    QString url = QString("%1/api/v4/categories?start=%2").arg(baseUrl).arg(start);
    
    getRequest(url, [callback](QNetworkReply* reply) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonArray jsonArray = doc.array();
        
        std::vector<Models::Category> categories;
        for (const auto& jsonValue : jsonArray) {
            categories.push_back(Models::Category::fromJson(jsonValue.toObject()));
        }
        
        callback(categories, true);
    }, errorCallback);
}

void VideoAPI::getSpecialTopics(int start, TopicCallback callback, ErrorCallback errorCallback) {
    QString url = QString("%1/api/v3/specialTopics?start=%2").arg(baseUrl).arg(start);
    
    getRequest(url, [callback](QNetworkReply* reply) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject json = doc.object();
        
        Models::TopicResponse response;
        response.count = json["count"].toInt();
        response.total = json["total"].toInt();
        response.nextPageUrl = json["nextPageUrl"].toString().toStdString();
        response.adExist = json["adExist"].toBool();
        
        if (json.contains("itemList")) {
            QJsonArray itemArray = json["itemList"].toArray();
            for (const auto& itemJson : itemArray) {
                QJsonObject itemObj = itemJson.toObject();
                Models::TopicItem item;
                
                item.type = itemObj["type"].toString().toStdString();
                item.id = itemObj["id"].toInt();
                item.adIndex = itemObj["adIndex"].toInt();
                
                if (itemObj.contains("data")) {
                    QJsonObject dataObj = itemObj["data"].toObject();
                    item.dataType = dataObj["dataType"].toString().toStdString();
                    item.title = dataObj["title"].toString().toStdString();
                    item.description = dataObj["description"].toString().toStdString();
                    item.image = dataObj["image"].toString().toStdString();
                    item.actionUrl = dataObj["actionUrl"].toString().toStdString();
                    item.shade = dataObj["shade"].toBool();
                    item.autoPlay = dataObj["autoPlay"].toBool();
                    
                    if (dataObj.contains("label")) {
                        QJsonObject labelObj = dataObj["label"].toObject();
                        item.label.text = labelObj["text"].toString().toStdString();
                        item.label.card = labelObj["card"].toString().toStdString();
                    }
                }
                
                response.itemList.push_back(item);
            }
        }
        
        callback(response, true);
    }, errorCallback);
}

void VideoAPI::getRankList(RankCallback callback, ErrorCallback errorCallback, const QString& strategy) {
    QString url = QString("%1/api/v4/rankList/videos?strategy=%2").arg(baseUrl).arg(strategy);
    
    getRequest(url, [callback](QNetworkReply* reply) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject json = doc.object();
        
        Models::RankResponse response;
        response.count = json["count"].toInt();
        response.total = json["total"].toInt();
        response.nextPageUrl = json["nextPageUrl"].toString().toStdString();
        response.adExist = json["adExist"].toBool();
        
        if (json.contains("itemList")) {
            QJsonArray itemArray = json["itemList"].toArray();
            for (const auto& itemJson : itemArray) {
                response.itemList.push_back(Models::ListItem::fromJson(itemJson.toObject()));
            }
        }
        
        callback(response, true);
    }, errorCallback);
}

void VideoAPI::getVideoDetail(int videoId, FeedCallback callback, ErrorCallback errorCallback) {
    QString url = QString("%1/api/v4/video/related?id=%2").arg(baseUrl).arg(videoId);
    getFeed(videoId, callback, errorCallback); // 复用getFeed的解析逻辑
}

void VideoAPI::getRequest(const QString& url, const std::function<void(QNetworkReply*)>& successHandler, ErrorCallback errorCallback) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply* reply = networkManager->get(request);
    
    QObject::connect(reply, &QNetworkReply::finished, [reply, successHandler, errorCallback]() {
        if (reply->error() == QNetworkReply::NoError) {
            successHandler(reply);
        } else {
            QString errorMsg = reply->errorString();
            qWarning() << "Network error:" << errorMsg;
            if (errorCallback) {
                errorCallback(errorMsg);
            }
        }
        reply->deleteLater();
    });
}

} // namespace API
} // namespace Mixed
