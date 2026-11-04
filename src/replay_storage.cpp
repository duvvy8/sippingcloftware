#include "replay_storage.h"
#include "platform.h"
namespace clip {
namespace {
QString creation(DWORD pid, bool *alive = nullptr) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) {
        if (alive)
            *alive = GetLastError() == ERROR_ACCESS_DENIED;
        return {};
    }
    FILETIME created{}, exit{}, kernel{}, user{};
    DWORD code = 0;
    bool ok = GetProcessTimes(p, &created, &exit, &kernel, &user) && GetExitCodeProcess(p, &code);
    if (alive)
        *alive = ok && code == STILL_ACTIVE;
    CloseHandle(p);
    return ok ? QString::number((quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime) : QString();
}
bool sameFile(const QJsonObject &a, const QJsonObject &b) {
    return !a.isEmpty() && !b.isEmpty() && a["volume"] == b["volume"] && a["file"] == b["file"] &&
           a["created"] == b["created"];
}
bool safeName(const QString &name) {
    static QRegularExpression valid(
        "^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}\\.mkv(?:\\.partial)?$");
    return valid.match(name).hasMatch();
}
void removeEntry(const QString &folder, const QJsonObject &entry) {
    QString name = entry["name"].toString();
    if (name.isEmpty())
        return;
    if (!safeName(name))
        throw std::runtime_error("Invalid replay journal filename; no unknown data was removed");
    QStringList names{name};
    if (name.endsWith(".partial"))
        names << name.chopped(8);
    for (const auto &candidate : names) {
        const auto path = QDir(folder).filePath(candidate);
        if (!QFileInfo::exists(path))
            continue;
        auto actual = fileIdentity(path);
        if (!sameFile(entry["identity"].toObject(), actual) ||
            (!name.endsWith(".partial") && entry["identity"].toObject() != actual))
            throw std::runtime_error("An old replay file has an unknown identity. It was preserved; review "
                                     "the replay folder before recording again.");
        if (!QFile::remove(path))
            throw std::runtime_error(
                "Old replay data could not be removed; check folder access and file locks");
    }
}
} // namespace
void ReplayStorage::persist() {
    if (!writeJson(QDir(directory_).filePath("session.json"), journal_))
        throw std::runtime_error("Replay journal could not be persisted; recording stopped safely");
}
void ReplayStorage::recover(const QString &root, const QString &token) {
    const auto canonical = QDir(root).canonicalPath();
    for (const auto &info :
         QDir(root).entryInfoList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (info.isJunction() || info.isSymLink())
            continue;
        const auto folder = info.canonicalFilePath();
        if (QFileInfo(folder).dir().canonicalPath() != canonical)
            throw std::runtime_error("Replay directory escaped its configured root");
        auto journal = readJson(QDir(folder).filePath("session.json"));
        if (journal["owner"].toString() != Owner)
            continue;
        bool alive = false;
        auto stamp = creation(DWORD(journal["pid"].toInteger()), &alive);
        if (alive && (stamp.isEmpty() || stamp == journal["processCreated"].toString()) &&
            journal["instance"].toString() != token)
            throw std::runtime_error("Another running application still owns replay data in this folder");
        for (auto value : journal["segments"].toArray())
            removeEntry(folder, value.toObject());
        removeEntry(folder, journal["active"].toObject());
        for (const auto &file : QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot))
            if (file != "session.json")
                throw std::runtime_error(
                    "An old replay directory contains an unknown file; it was preserved for review");
        if (!QFile::remove(QDir(folder).filePath("session.json")) || !QDir().rmdir(folder))
            throw std::runtime_error("Cannot finish cleaning old replay storage");
    }
}
void ReplayStorage::begin(const QString &root, const QString &token) {
    root_ = QFileInfo(root).absoluteFilePath();
    if (!QDir().mkpath(root_))
        throw std::runtime_error("Cannot create replay storage folder");
    lock_ = std::make_unique<QLockFile>(QDir(root_).filePath(".replay.lock"));
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock())
        throw std::runtime_error("Replay storage is in use by another capture session");
    token_ = token;
    recover(root_, token);
    directory_ = QDir(root_).filePath("session-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(directory_))
        throw std::runtime_error("Cannot create replay session directory");
    journal_ = {{"owner", Owner},
                {"instance", token_},
                {"pid", qint64(GetCurrentProcessId())},
                {"processCreated", creation(GetCurrentProcessId())},
                {"state", "Recording"},
                {"segments", QJsonArray()}};
    persist();
}
QString ReplayStorage::reserve() {
    auto name = QUuid::createUuid().toString(QUuid::WithoutBraces) + ".mkv.partial";
    journal_["active"] = QJsonObject{{"name", name}};
    persist();
    return QDir(directory_).filePath(name);
}
void ReplayStorage::identifyActive() {
    auto active = journal_["active"].toObject();
    active["identity"] = fileIdentity(QDir(directory_).filePath(active["name"].toString()));
    if (active["identity"].toObject().isEmpty())
        throw std::runtime_error("Cannot identify the active replay file");
    journal_["active"] = active;
    persist();
}
void ReplayStorage::committed(const QList<Segment> &segments) {
    QJsonArray array;
    for (const auto &s : segments)
        array << QJsonObject{{"name", QFileInfo(s.path).fileName()}, {"identity", s.identity}};
    journal_["segments"] = array;
    journal_.remove("active");
    persist();
}
void ReplayStorage::stop() noexcept {
    if (directory_.isEmpty())
        return;
    journal_["state"] = "Stopped";
    try {
        persist();
    } catch (const std::exception &e) {
        qWarning("Replay journal finalization: %s", e.what());
    }
}
} // namespace clip
