//
// Created by Anlk on 2026/6/3.
// 响应数据模型
//

#ifndef MIXEDEDITING_RESPONSES_H
#define MIXEDEDITING_RESPONSES_H

#include <string>
#include <vector>
#include "VideoData.h"

namespace Mixed {
namespace Models {

struct ListItem {
    std::string type;
    VideoData data;
    int id = 0;
    int adIndex = -1;

    static ListItem fromJson(const QJsonObject& json);
};

struct IssueItem {
    long long releaseTime = 0;
    std::string type;
    int date = 0;
    long long publishTime = 0;
    std::vector<ListItem> itemList;
    int count = 0;

    static IssueItem fromJson(const QJsonObject& json);
};

struct FeedResponse {
    std::vector<IssueItem> issueList;
    std::string nextPageUrl;
    long long nextPublishTime = 0;
    std::string newestIssueType;

    static FeedResponse fromJson(const QJsonObject& json);
};

struct Category {
    int id = 0;
    std::string name;
    std::string description;
    std::string bgPicture;
    std::string bgColor;
    std::string headerImage;
    int defaultAuthorId = 0;
    int tagId = 0;

    static Category fromJson(const QJsonObject& json);
};

struct Label {
    std::string text;
    std::string card;

    static Label fromJson(const QJsonObject& json);
};

struct TopicItem {
    std::string type;
    std::string dataType;
    int id = 0;
    std::string title;
    std::string description;
    std::string image;
    std::string actionUrl;
    std::vector<std::string> adTrack;
    bool shade = false;
    Label label;
    std::vector<std::string> labelList;
    bool autoPlay = false;
    int adIndex = -1;

    static TopicItem fromJson(const QJsonObject& json);
};

struct TopicResponse {
    std::vector<TopicItem> itemList;
    int count = 0;
    int total = 0;
    std::string nextPageUrl;
    bool adExist = false;

    static TopicResponse fromJson(const QJsonObject& json);
};

struct RankResponse {
    std::vector<ListItem> itemList;
    int count = 0;
    int total = 0;
    std::string nextPageUrl;
    bool adExist = false;

    static RankResponse fromJson(const QJsonObject& json);
};

} // namespace Models
} // namespace Mixed

#endif //MIXEDEDITING_RESPONSES_H
