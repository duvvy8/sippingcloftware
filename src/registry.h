#pragma once
#include "media.h"
namespace clip {
struct Adapter {
    QString name, driver, luid;
    quint32 vendor = 0, device = 0;
    int index = 0;
    QJsonObject json() const;
};
struct Display {
    QString name, identity;
    quintptr handle = 0;
    int width = 0, height = 0, adapter = 0, output = 0;
    double refresh = 0;
    bool hdr = false;
    QJsonObject json() const;
};
struct EncoderCaps {
    QString name, codec, description, backend = "CPU", reason;
    int codecId = 0;
    QStringList pixelFormats, rateControls, containers;
    QJsonArray tested;
    bool included = true, hardware = false, hardwareMatched = false, initialized = false, encoded = false,
         live = false, hdr = false;
    double elapsedMs = 0;
    bool replaySuitable() const;
    QJsonObject json() const;
    static EncoderCaps fromJson(const QJsonObject &);
};
class Registry {
  public:
    QList<Adapter> adapters;
    QList<Display> displays;
    QList<EncoderCaps> encoders;
    QString recommended;
    void discover();
    void probe(std::function<void(QString)> progress = {}, std::atomic_bool *cancel = nullptr,
               bool useCache = true);
    QString cacheKey() const;
    bool loadCache();
    QString automatic(bool hdr = false) const;
    const EncoderCaps *find(const QString &) const;
    QString forCodec(int codecId, const QString &preferred = {}, bool hdr = false) const;
    QJsonObject json() const;
    static QJsonObject testEncoder(const QString &name, bool hdr = false);
};
} // namespace clip
