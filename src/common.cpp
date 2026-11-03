#include "common.h"
#include <stdexcept>
namespace clip {
QString avError(int c) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(c, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}
void check(int c, const char *context) {
    if (c < 0)
        throw std::runtime_error((QString::fromUtf8(context) + ": " + avError(c)).toStdString());
}
double clockSeconds() {
    LARGE_INTEGER n, f;
    QueryPerformanceCounter(&n);
    QueryPerformanceFrequency(&f);
    return double(n.QuadPart) / double(f.QuadPart);
}
double processCpuSeconds() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return 0;
    auto value = [](FILETIME t) {
        return double((quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 10000000.;
    };
    return value(kernel) + value(user);
}
QString stateDir() {
    const auto override = qEnvironmentVariable("CLIPPER_STATE_DIR");
    if (!override.isEmpty()) {
        QDir().mkpath(override);
        return override;
    }
    const auto path =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/DesktopClipper";
    QDir().mkpath(path);
    return path;
}
bool writeJson(const QString &p, const QJsonObject &o) {
    QDir().mkpath(QFileInfo(p).absolutePath());
    QSaveFile f(p);
    return f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(o).toJson()) >= 0 && f.commit();
}
QJsonObject readJson(const QString &p) {
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
QString safeName(QString s) {
    s.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "_");
    s = s.trimmed();
    while (s.endsWith('.') || s.endsWith(' '))
        s.chop(1);
    s = s.left(100);
    if (s.isEmpty())
        s = "Desktop";
    if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",
                           QRegularExpression::CaseInsensitiveOption)
            .match(s)
            .hasMatch())
        s.prepend('_');
    return s;
}
QString uniquePath(const QString &dir, const QString &stem, const QString &ext) {
    if (!QDir().mkpath(dir))
        throw std::runtime_error("Cannot create output folder");
    QString p;
    int i = 0;
    do {
        p = QDir(dir).filePath(safeName(stem) + (i ? QString("-%1").arg(i) : QString()) + '.' + ext);
        ++i;
    } while (QFileInfo::exists(p));
    return p;
}
QString ffmpegPath() {
    return QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
}
ProcessResult runProcess(const QString &exe, const QStringList &args, int timeout, std::atomic_bool *cancel,
                         std::function<void(QByteArray)> output) {
    QProcess p;
    p.setProgram(exe);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    });
    p.start();
    ProcessResult r;
    if (!p.waitForStarted(10000)) {
        r.output = p.errorString().toUtf8();
        return r;
    }
    QElapsedTimer timer;
    timer.start();
    while (p.state() != QProcess::NotRunning) {
        p.waitForReadyRead(50);
        auto bytes = p.readAll();
        if (output && !bytes.isEmpty())
            output(bytes);
        r.output += bytes;
        if (r.output.size() > 2 * 1024 * 1024)
            r.output = r.output.right(1024 * 1024);
        if ((cancel && cancel->load()) || (timeout > 0 && timer.elapsed() > timeout)) {
            r.cancelled = cancel && cancel->load();
            r.timedOut = !r.cancelled;
            p.kill();
            p.waitForFinished(5000);
            break;
        }
    }
    r.output += p.readAll();
    r.code = (p.exitStatus() == QProcess::NormalExit && !r.cancelled && !r.timedOut) ? p.exitCode() : -1;
    return r;
}
QJsonObject AudioSource::json() const {
    return {{"id", id},           {"name", name}, {"kind", kind},         {"pid", qint64(pid)},
            {"enabled", enabled}, {"mute", mute}, {"separate", separate}, {"mix", mix},
            {"ptt", pushToTalk},  {"gain", gain}, {"pushKey", pushKey},   {"pushModifiers", pushModifiers}};
}
AudioSource AudioSource::fromJson(const QJsonObject &o) {
    AudioSource a;
    a.id = o["id"].toString();
    a.name = o["name"].toString();
    a.kind = o["kind"].toString("system");
    a.pid = quint32(o["pid"].toInteger());
    a.enabled = o["enabled"].toBool(true);
    a.mute = o["mute"].toBool();
    a.separate = o["separate"].toBool();
    a.mix = o["mix"].toBool(true);
    a.pushToTalk = o["ptt"].toBool();
    a.gain = o["gain"].toDouble(1);
    a.pushKey = o["pushKey"].toInt('V');
    a.pushModifiers = o["pushModifiers"].toInt();
    return a;
}
Settings::Settings() {
    library = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Clips";
    bufferFolder = stateDir() + "/buffer";
}
QJsonObject Settings::json() const {
    QJsonArray sources;
    for (const auto &a : audio)
        sources.append(a.json());
    QJsonObject folders;
    for (auto i = folderOverrides.begin(); i != folderOverrides.end(); ++i)
        folders[i.key()] = i.value();
    return {{"library", library},
            {"bufferFolder", bufferFolder},
            {"encoder", encoder},
            {"backend", backend},
            {"source", source},
            {"container", container},
            {"quality", quality},
            {"monitor", monitor},
            {"fps", fps},
            {"height", height},
            {"retention", retention},
            {"bufferBytes", bufferBytes},
            {"hdr", hdr},
            {"startup", startup},
            {"autoRecord", autoRecord},
            {"notifications", notifications},
            {"overlayHotkey", overlayHotkey},
            {"recordHotkey", recordHotkey},
            {"pttHotkey", pttHotkey},
            {"audio", sources},
            {"folders", folders}};
}
void Settings::load() {
    loadObject(readJson(stateDir() + "/settings.json"));
}
void Settings::loadObject(const QJsonObject &o) {
    if (o.isEmpty())
        return;
    library = o["library"].toString(library);
    bufferFolder = o["bufferFolder"].toString(bufferFolder);
    encoder = o["encoder"].toString(encoder);
    backend = o["backend"].toString(backend);
    source = o["source"].toString(source);
    container = o["container"].toString(container);
    quality = o["quality"].toString(quality);
    monitor = o["monitor"].toInt();
    fps = o["fps"].toInt(60);
    height = o["height"].toInt();
    retention = o["retention"].toInt(300);
    bufferBytes = o["bufferBytes"].toInteger(bufferBytes);
    hdr = o["hdr"].toBool();
    startup = o["startup"].toBool();
    autoRecord = o["autoRecord"].toBool();
    notifications = o["notifications"].toBool(true);
    overlayHotkey = o["overlayHotkey"].toString(overlayHotkey);
    recordHotkey = o["recordHotkey"].toString(recordHotkey);
    pttHotkey = o["pttHotkey"].toString(pttHotkey);
    audio.clear();
    for (auto a : o["audio"].toArray())
        audio << AudioSource::fromJson(a.toObject());
    const auto folders = o["folders"].toObject();
    for (auto i = folders.begin(); i != folders.end(); ++i)
        folderOverrides[i.key()] = i.value().toString();
}
bool Settings::save() const {
    return writeJson(stateDir() + "/settings.json", json());
}
} // namespace clip
