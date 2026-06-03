//
// Created by Anlk on 2026/6/3.
// 提供者数据模型
//

#ifndef MIXEDEDITING_PROVIDER_H
#define MIXEDEDITING_PROVIDER_H

#include <string>
#include <QJsonObject>

namespace Mixed {
namespace Models {

struct Provider {
    std::string name;
    std::string alias;
    std::string icon;

    static Provider fromJson(const QJsonObject& json);
};

} // namespace Models
} // namespace Mixed

#endif //MIXEDEDITING_PROVIDER_H
