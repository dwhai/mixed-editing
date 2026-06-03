//
// Created by Anlk on 2026/6/3.
// 标签数据模型
//

#ifndef MIXEDEDITING_TAG_H
#define MIXEDEDITING_TAG_H

#include <string>
#include <QJsonObject>

namespace Mixed {
namespace Models {

struct Tag {
    int id = 0;
    std::string name;
    std::string actionUrl;
    std::string desc;
    std::string bgPicture;
    std::string headerImage;
    std::string tagRecType;
    bool haveReward = false;
    bool ifNewest = false;
    int communityIndex = 0;

    static Tag fromJson(const QJsonObject& json);
};

} // namespace Models
} // namespace Mixed

#endif //MIXEDEDITING_TAG_H
