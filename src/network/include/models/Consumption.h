//
// Created by Anlk on 2026/6/3.
// 消费数据模型
//

#ifndef MIXEDEDITING_CONSUMPTION_H
#define MIXEDEDITING_CONSUMPTION_H

#include <QJsonObject>

namespace Mixed {
namespace Models {

struct Consumption {
    int collectionCount = 0;
    int shareCount = 0;
    int replyCount = 0;
    int realCollectionCount = 0;

    static Consumption fromJson(const QJsonObject& json);
};

} // namespace Models
} // namespace Mixed

#endif //MIXEDEDITING_CONSUMPTION_H
