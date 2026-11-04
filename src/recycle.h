#pragma once
#include "common.h"
namespace clip {
bool stageForRecycling(const QString &source, const QString &stage, const QJsonObject &expected,
                       QString *error = nullptr);
// Calls the Shell's explicit RecycleItem operation. There is no RemoveItem/DeleteFile fallback.
bool recycleOnly(const QString &path, const QJsonObject &expected, QString *destination = nullptr,
                 QString *error = nullptr);
} // namespace clip
