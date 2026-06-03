//
// Created by Anlk on 2026/6/3.
// 封面和视频播放数据模型
//

#ifndef MIXEDEDITING_VIDEOINFO_H
#define MIXEDEDITING_VIDEOINFO_H

#include <string>
#include <vector>
#include <QJsonObject>

namespace Mixed {
namespace Models {

struct Cover {
    std::string feed;
    std::string detail;
    std::string blurred;
    std::string homepage;

    static Cover fromJson(const QJsonObject& json);
};

struct UrlItem {
    std::string name;
    std::string url;
    int size = 0;

    static UrlItem fromJson(const QJsonObject& json);
};

struct PlayInfo {
    int height = 0;
    int width = 0;
    std::string name;
    std::string type;
    std::string url;
    std::vector<UrlItem> urlList;

    static PlayInfo fromJson(const QJsonObject& json);
};

struct WebUrl {
    std::string raw;
    std::string forWeibo;

    static WebUrl fromJson(const QJsonObject& json);
};

} // namespace Models
} // namespace Mixed

#endif //MIXEDEDITING_VIDEOINFO_H
