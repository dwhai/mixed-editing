//
// Created by Anlk on 2026/6/3.
// 视频数据模型
//

#ifndef MIXEDEDITING_VIDEODATA_H
#define MIXEDEDITING_VIDEODATA_H

#include <string>
#include <vector>
#include "Tag.h"
#include "Consumption.h"
#include "Provider.h"
#include "Author.h"
#include "VideoInfo.h"

namespace Mixed {
namespace Models {

struct VideoData {
    std::string dataType;
    int id = 0;
    std::string title;
    std::string description;
    std::string library;
    std::vector<Tag> tags;
    Consumption consumption;
    std::string resourceType;
    std::string slogan;
    Provider provider;
    std::string category;
    Author author;
    Cover cover;
    std::string playUrl;
    std::string thumbPlayUrl;
    int duration = 0;
    WebUrl webUrl;
    long long releaseTime = 0;
    std::vector<PlayInfo> playInfo;
    bool ad = false;
    std::string type;
    std::string descriptionEditor;
    bool collected = false;
    bool reallyCollected = false;
    bool played = false;

    static VideoData fromJson(const QJsonObject& json);
};

} // namespace Models
} // namespace Mixed

#endif //MIXEDEDITING_VIDEODATA_H
