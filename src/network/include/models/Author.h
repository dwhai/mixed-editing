//
// Created by Anlk on 2026/6/3.
// 作者数据模型
//

#ifndef MIXEDEDITING_AUTHOR_H
#define MIXEDEDITING_AUTHOR_H

#include <string>
#include <QJsonObject>

namespace Mixed {
namespace Models {

struct Follow {
    std::string itemType;
    int itemId = 0;
    bool followed = false;

    static Follow fromJson(const QJsonObject& json);
};

struct Shield {
    std::string itemType;
    int itemId = 0;
    bool shielded = false;

    static Shield fromJson(const QJsonObject& json);
};

struct Author {
    int id = 0;
    std::string icon;
    std::string name;
    std::string description;
    std::string link;
    long long latestReleaseTime = 0;
    int videoNum = 0;
    Follow follow;
    Shield shield;
    bool ifPgc = false;

    static Author fromJson(const QJsonObject& json);
};

} // namespace Models
} // namespace Mixed

#endif //MIXEDEDITING_AUTHOR_H
