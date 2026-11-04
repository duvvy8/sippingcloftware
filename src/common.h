#pragma once
#include <QtCore>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <windows.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
namespace clip {
constexpr qint64 SizeLimit = 20000000, TargetSize = 19500000;
QString avError(int code);
void check(int code, const char *context);
double clockSeconds();
double processCpuSeconds();
QString stateDir();
bool writeJson(const QString &path, const QJsonObject &object);
QJsonObject readJson(const QString &path);
QString safeName(QString value);
QString uniquePath(const QString &folder, const QString &stem, const QString &extension);
QString ffmpegPath();
struct ProcessResult {
    int code = -1;
    QByteArray output;
    bool cancelled = false, timedOut = false;
};
ProcessResult runProcess(const QString &program, const QStringList &args, int timeoutMs = 0,
                         std::atomic_bool *cancel = nullptr, std::function<void(QByteArray)> output = {},
                         const QString &workingDirectory = {});
struct AudioSource {
    QString id, name, kind = "system";
    quint32 pid = 0;
    bool enabled = true, mute = false, separate = false, mix = true, pushToTalk = false;
    double gain = 1.;
    int pushKey = 'V', pushModifiers = 0;
    QJsonObject json() const;
    static AudioSource fromJson(const QJsonObject &);
};
struct Settings {
    QString library, bufferFolder, encoder = "auto", backend = "wgc", source = "monitor", container = "mp4",
                                   quality = "High";
    int monitor = 0, fps = 60, height = 0, retention = 300;
    quintptr window = 0;
    qint64 bufferBytes = 4LL * 1024 * 1024 * 1024;
    bool hdr = false, toneMapCapture = false, startup = false, autoRecord = false, notifications = true;
    QString overlayHotkey = "Ctrl+Shift+S", recordHotkey = "Ctrl+Shift+R", pttHotkey = "V";
    QList<AudioSource> audio;
    QMap<QString, QString> folderOverrides;
    Settings();
    QJsonObject json() const;
    void load();
    void loadObject(const QJsonObject &);
    bool save() const;
};
} // namespace clip
