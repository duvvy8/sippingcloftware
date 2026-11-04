#include "library.h"
#include "platform.h"
#include "replay_storage.h"
namespace clip {
namespace {
QString metadataPath(const QString &root, const QJsonObject &identity) {
    if (identity.isEmpty())
        return {};
    const auto key = identity["volume"].toString() + '-' + identity["file"].toString() + '-' +
                     identity["created"].toString();
    return QDir(root).filePath("Metadata/" + key + ".clip.json");
}
}
QJsonObject readClipMetadata(const QString &root, const QString &path) {
    const auto central = metadataPath(root, fileIdentity(path));
    const auto metadata = central.isEmpty() ? QJsonObject{} : readJson(central);
    return metadata.isEmpty() ? readJson(path + ".clip.json") : metadata;
}
bool writeClipMetadata(const QString &root, const QString &path, const QJsonObject &metadata) {
    const auto central = metadataPath(root, fileIdentity(path));
    return !central.isEmpty() && writeJson(central, metadata);
}
QJsonObject ClipRecord::json() const {
    return {{"path", path},       {"application", application},
            {"created", created}, {"relationship", relationship},
            {"parent", parent},   {"id", id},
            {"media", media},     {"identity", identity}};
}
ClipRecord ClipRecord::fromJson(const QJsonObject &o) {
    ClipRecord r;
    r.path = o["path"].toString();
    r.application = o["application"].toString();
    r.created = o["created"].toString();
    r.relationship = o["relationship"].toString("original");
    r.parent = o["parent"].toString();
    r.id = o["id"].toString();
    r.media = o["media"].toObject();
    r.identity = o["identity"].toObject();
    return r;
}
Library::Library(QObject *p) : QObject(p) {
    thumbnails_.setMaxThreadCount(1);
    thumbnails_.setExpiryTimeout(1000);
    connect(this, &Library::thumbnailReady, this, [this](const QString &p) { pending_.remove(p); });
}
Library::~Library() {
    cancel_ = true;
    if (scan_.joinable())
        scan_.join();
    thumbnails_.clear();
    thumbnails_.waitForDone();
}
void Library::refresh(const QString &root) {
    cancel_ = true;
    if (scan_.joinable())
        scan_.join();
    cancel_ = false;
    emit status("Reading clip index…");
    scan_ = std::thread([this, root] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        QList<ClipRecord> items;
        QMap<QString, ClipRecord> byIdentity;
        auto cache = readJson(stateDir() + "/library-index.json");
        for (auto v : cache["clips"].toArray()) {
            auto r = ClipRecord::fromJson(v.toObject());
            byIdentity[r.identity["volume"].toString() + ":" + r.identity["file"].toString()] = r;
        }
        QStringList roots{root};
        if (QDir(root) == QDir(defaultClipLibrary()))
            roots.append(legacyClipLibrary());
        for (const auto &folder : roots) {
            QDirIterator it(folder, {"*.mp4", "*.mkv", "*.webm", "*.mov"}, QDir::Files,
                            QDirIterator::Subdirectories);
            while (it.hasNext() && !cancel_) {
                auto path = it.next();
                if (readJson(QFileInfo(path).dir().filePath("session.json"))["owner"].toString() ==
                    ReplayStorage::Owner)
                    continue;
                auto identity = fileIdentity(path);
                auto key = identity["volume"].toString() + ":" + identity["file"].toString();
                ClipRecord r;
                if (byIdentity.contains(key) && byIdentity[key].identity == identity) {
                    r = byIdentity[key];
                    r.path = path;
                } else
                    try {
                        r.path = path;
                        r.identity = identity;
                        auto side = readClipMetadata(root, path);
                        r.media = inspect(path).json();
                        r.application = side["application"].toString(QFileInfo(path).dir().dirName());
                        r.created = side["created"].toString(
                            QFileInfo(path).birthTime().toUTC().toString(Qt::ISODateWithMs));
                        r.relationship = side["relationship"].toString("original");
                        r.parent = side["parent"].toString();
                        r.id = side["id"].toString();
                    } catch (const std::exception &) {
                        continue;
                    }
                items << r;
            }
        }
        if (cancel_)
            return;
        std::sort(items.begin(), items.end(),
                  [](const auto &a, const auto &b) { return a.created > b.created; });
        QJsonArray array;
        for (auto &r : items)
            array << r.json();
        writeJson(stateDir() + "/library-index.json", {{"root", root}, {"clips", array}});
        QMetaObject::invokeMethod(
            this,
            [this, items] {
                records_ = items;
                emit refreshed();
                emit status(QString("%1 saved clips").arg(items.size()));
            },
            Qt::QueuedConnection);
    });
}
void Library::requestThumbnail(const QString &path) {
    if (pending_.contains(path))
        return;
    pending_.insert(path);
    auto identity = fileIdentity(path);
    auto key = QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact),
                                        QCryptographicHash::Sha256)
                   .toHex();
    QString cache = stateDir() + "/thumbnails/" + key + ".jpg";
    thumbnails_.start(QRunnable::create([this, path, cache] {
        QImage image;
        if (!image.load(cache)) {
            image = thumbnail(path, 0.5);
            if (!image.isNull()) {
                QDir().mkpath(QFileInfo(cache).absolutePath());
                image.save(cache, "JPG", 85);
            }
        }
        emit thumbnailReady(path, image);
    }));
}
bool Library::rename(const QString &path, const QString &name, QString *error) {
    QString stem = safeName(name);
    QString target = QFileInfo(path).dir().filePath(stem + '.' + QFileInfo(path).suffix());
    if (target == path)
        return true;
    if (QFileInfo::exists(target) || !QFile::rename(path, target)) {
        if (error)
            *error = "Could not rename the clip. Choose an unused name and close applications using it.";
        return false;
    }
    if (QFileInfo::exists(path + ".clip.json"))
        QFile::rename(path + ".clip.json", target + ".clip.json");
    for (auto &r : records_)
        if (r.path == path)
            r.path = target;
    emit refreshed();
    return true;
}
} // namespace clip
