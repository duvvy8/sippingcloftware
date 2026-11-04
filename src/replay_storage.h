#pragma once
#include "replay.h"
namespace clip {
class ReplayStorage {
    QString root_, directory_, token_;
    QJsonObject journal_;
    std::unique_ptr<QLockFile> lock_;
    void persist();

  public:
    static constexpr const char *Owner = "5f596c3b-561a-4f80-a84c-653a7456b83e";
    void begin(const QString &root, const QString &instanceToken);
    QString reserve();
    void identifyActive();
    void committed(const QList<Segment> &);
    void stop() noexcept;
    static void recover(const QString &root, const QString &instanceToken);
};
} // namespace clip
