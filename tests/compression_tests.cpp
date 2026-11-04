#include "compression.h"
#include "jobs.h"
#include "notifications.h"
#include "platform.h"
#include <QtTest>
class ClipboardHolder {
    HANDLE event_ = nullptr;
    QProcess process_;
    QString ready_;

  public:
    bool start(const QString &folder) {
        QString name = "Local\\ClipperClipboard-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        event_ = CreateEventW(nullptr, TRUE, FALSE, name.toStdWString().c_str());
        if (!event_)
            return false;
        ready_ = QDir(folder).filePath("clipboard-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                       ".json");
        process_.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments *p) { p->flags |= CREATE_NO_WINDOW; });
        process_.start(QCoreApplication::applicationDirPath() + "/clipboard_fixture.exe", {ready_, name});
        return process_.waitForStarted(3000);
    }
    bool ready() const {
        return clip::readJson(ready_)["locked"].toBool();
    }
    void release() {
        if (event_)
            SetEvent(event_);
        if (process_.state() != QProcess::NotRunning)
            process_.waitForFinished(5000);
    }
    ~ClipboardHolder() {
        release();
        if (event_)
            CloseHandle(event_);
    }
};
class CompressionTests : public QObject {
    Q_OBJECT
    QTemporaryDir tmp_;
    QString source_;
    QByteArray digest(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        QCryptographicHash h(QCryptographicHash::Sha256);
        h.addData(&f);
        return h.result();
    }
  private slots:
    void initTestCase() {
        source_ = tmp_.filePath("source.mp4");
        auto p = clip::runProcess(clip::ffmpegPath(),
                                  {"-v",
                                   "error",
                                   "-y",
                                   "-f",
                                   "lavfi",
                                   "-i",
                                   "testsrc2=size=320x180:rate=30",
                                   "-f",
                                   "lavfi",
                                   "-i",
                                   "sine=frequency=440:sample_rate=48000",
                                   "-t",
                                   "7",
                                   "-c:v",
                                   "libx264",
                                   "-preset",
                                   "ultrafast",
                                   "-crf",
                                   "12",
                                   "-c:a",
                                   "aac",
                                   "-color_primaries",
                                   "bt709",
                                   "-color_trc",
                                   "bt709",
                                   "-colorspace",
                                   "bt709",
                                   "-color_range",
                                   "tv",
                                   source_},
                                  30000);
        QVERIFY2(p.code == 0, p.output.constData());
    }
    void candidatePolicy() {
        clip::MediaInfo m;
        m.width = 3840;
        m.height = 2160;
        m.fps = 120;
        m.duration = 60;
        auto c = clip::compressionCandidates(m, 19000000);
        QVERIFY(c.size() > 40);
        QCOMPARE(c[0].width, 3840);
        QCOMPARE(c[0].fps, 120.);
        QVERIFY(c[0].bitrate > c[1].bitrate && c[1].bitrate > c[2].bitrate);
        QCOMPARE(c[3].fps, 60.);
        QCOMPARE(c[3].height, 2160);
        QCOMPARE(c[6].height, 1440);
        QCOMPARE(c[6].fps, 60.);
        bool thirty = false;
        for (auto v : c) {
            QVERIFY(v.width <= m.width && v.height <= m.height && v.fps <= m.fps);
            QVERIFY(v.height >= 480 && v.fps >= 15);
            if (v.fps == 30 && !thirty) {
                QCOMPARE(v.height, 2160);
                thirty = true;
            }
        }
        QVERIFY(thirty);
        m.width = 240;
        m.height = 426;
        m.fps = 12;
        c = clip::compressionCandidates(m, 19000000);
        QCOMPARE(c.size(), 3);
        for (auto v : c) {
            QCOMPARE(v.width, 240);
            QCOMPARE(v.height, 426);
            QCOMPARE(v.fps, 12.);
        }
    }
    void scoreThresholds() {
        QJsonArray frames;
        for (int i = 0; i < 100; ++i)
            frames << QJsonObject{{"metrics", QJsonObject{{"vmaf", i < 5 ? 79. : 100.}}}};
        auto score = clip::readQualityScore({{"frames", frames}});
        QVERIFY(score.mean >= 90);
        QCOMPARE(score.fifthPercentile, 79.);
        QVERIFY(!score.passed());
    }
    void alreadySmallAndExactBoundary() {
        auto before = digest(source_);
        auto r = clip::compressMedia(source_, tmp_.filePath("unused.mp4"), "libx264");
        QCOMPARE(r.state, QString("Already within limit"));
        QCOMPARE(r.output, source_);
        QVERIFY(!QFileInfo::exists(tmp_.filePath("unused.mp4")));
        QCOMPARE(digest(source_), before);
        QString boundary = tmp_.filePath("boundary.mp4");
        QVERIFY(QFile::copy(source_, boundary));
        QFile file(boundary);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(clip::SizeLimit));
        file.close();
        r = clip::compressMedia(boundary, tmp_.filePath("boundary-out.mp4"), "libx264");
        QCOMPARE(r.state, QString("Already within limit"));
        QVERIFY(!QFileInfo::exists(tmp_.filePath("boundary-out.mp4")));
    }
    void completeCompression() {
        QString padded = tmp_.filePath("padded.mp4");
        QVERIFY(QFile::copy(source_, padded));
        QFile file(padded);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(clip::SizeLimit + 1));
        file.close();
        auto before = digest(padded);
        QString output = tmp_.filePath("compressed.mp4");
        auto r = clip::compressMedia(padded, output, "libx264");
        QVERIFY2(r.state == "Compressed", qPrintable(r.state + ": " + r.reason));
        QVERIFY(r.media.bytes <= clip::SizeLimit);
        QVERIFY(r.quality.passed());
        QCOMPARE(r.media.audioTracks, 1);
        QVERIFY(qAbs(r.media.duration - clip::inspect(padded).duration) <= 1. / 30 + .025);
        QCOMPARE(digest(padded), before);
        QVERIFY(clip::decodeValidate(output));
        QVERIFY(QDir(tmp_.path()).entryList({".compress-*"}, QDir::Dirs | QDir::Hidden).isEmpty());
    }
    void severeDegradationRejected() {
        QString bad = tmp_.filePath("degraded.mp4");
        auto p = clip::runProcess(clip::ffmpegPath(),
                                  {"-v", "error", "-y", "-i", source_, "-an", "-vf", "scale=32:18", "-c:v",
                                   "libx264", "-crf", "45", bad},
                                  30000);
        QVERIFY2(p.code == 0, p.output.constData());
        auto score = clip::scoreMedia(source_, bad, clip::inspect(source_));
        QVERIFY(!score.passed());
        QVERIFY(score.frames >= 208);
    }
    void cancellationRetainsOriginal() {
        std::atomic_bool cancel = true;
        auto before = digest(source_);
        QVERIFY_THROWS_EXCEPTION(
            std::runtime_error,
            clip::compressMedia(source_, tmp_.filePath("cancelled.mp4"), "libx264", &cancel));
        QCOMPARE(digest(source_), before);
        QVERIFY(!QFileInfo::exists(tmp_.filePath("cancelled.mp4")));
    }
    void audioBudgetFailure() {
        auto original = tmp_.filePath("audio-budget.mkv");
        auto p = clip::runProcess(clip::ffmpegPath(),
                                  {"-v", "error", "-y", "-f", "lavfi", "-i", "color=size=16x16:rate=15", "-f",
                                   "lavfi", "-i", "anullsrc=r=48000:cl=stereo", "-t", "70", "-c:v", "ffv1",
                                   "-c:a", "pcm_s32le", original},
                                  30000);
        QVERIFY2(p.code == 0, p.output.constData());
        auto before = digest(original);
        auto result = clip::compressMedia(original, tmp_.filePath("audio-budget-out.mkv"), "ffv1");
        QCOMPARE(result.state, QString("Cannot meet constraints"));
        QVERIFY(result.reason.contains("audio"));
        QVERIFY(result.attempts.isEmpty());
        QCOMPARE(digest(original), before);
    }
    void losslessConstraintFailure() {
        auto original = tmp_.filePath("lossless.mkv");
        auto p = clip::runProcess(clip::ffmpegPath(),
                                  {"-v", "error", "-y", "-f", "lavfi", "-i",
                                   "testsrc2=size=640x360:rate=30,noise=alls=100:allf=t+u", "-t", "3", "-an",
                                   "-c:v", "ffv1", "-level", "3", original},
                                  30000);
        QVERIFY2(p.code == 0, p.output.constData());
        QVERIFY(QFileInfo(original).size() > clip::SizeLimit);
        auto before = digest(original);
        auto output = tmp_.filePath("lossless-out.mkv");
        auto result = clip::compressMedia(original, output, "ffv1");
        QCOMPARE(result.state, QString("Cannot meet constraints"));
        QVERIFY(!result.attempts.isEmpty());
        QVERIFY(!QFileInfo::exists(output));
        QCOMPARE(digest(original), before);
    }
    void cancellationDuringCandidate() {
        auto original = tmp_.filePath("cancel-during.mp4");
        QVERIFY(QFile::copy(source_, original));
        QFile f(original);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.resize(clip::SizeLimit + 1));
        f.close();
        auto before = digest(original);
        std::atomic_bool cancel = false;
        auto output = tmp_.filePath("cancel-during-out.mp4");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, clip::compressMedia(original, output, "libx264", &cancel,
                                                                         [&](QString state, QString, double) {
                                                                             if (state == "Encoding")
                                                                                 cancel = true;
                                                                         }));
        QCOMPARE(digest(original), before);
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(QDir(tmp_.path()).entryList({".compress-*"}, QDir::Dirs | QDir::Hidden).isEmpty());
    }
    void queuedSaveAndCompression() {
        auto source = tmp_.filePath("replay-source.mp4");
        QVERIFY(QFile::copy(source_, source));
        clip::ReplayBuffer buffer;
        buffer.begin(0, 300, 1024 * 1024 * 1024);
        buffer.add({source, 0, 7, QFileInfo(source).size()});
        auto pin = buffer.pin(6, 3);
        clip::Settings settings;
        settings.library = tmp_.filePath("queue-output");
        settings.encoder = "libx264";
        settings.fps = 30;
        clip::ExportQueue queue(&buffer, nullptr, tmp_.filePath("queue-jobs"));
        auto id = queue.submitSave(pin, settings, "Fixture", "", true);
        Q_UNUSED(id);
        settings.fps = 15;
        QTRY_VERIFY_WITH_TIMEOUT(!queue.jobs().isEmpty() && queue.jobs().first().terminal, 30000);
        auto job = queue.jobs().first();
        QCOMPARE(job.state, QString("Already within limit"));
        QCOMPARE(job.output, job.original);
        QVERIFY(job.details["copied"].toBool());
        QCOMPARE(job.details["deleteDecision"].toString(), QString("Not applicable"));
        QVERIFY(qAbs(clip::inspect(job.original).duration - 3.) < .06);
        QCOMPARE(clip::inspect(job.original).fps, 30.);
        queue.shutdown();
        buffer.stop();
        buffer.clear();
    }
    void clipboardFailureAndRetry() {
        auto original = tmp_.filePath("clipboard-original.mp4");
        QVERIFY(QFile::copy(source_, original));
        QFile file(original);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(clip::SizeLimit + 1));
        file.close();
        auto before = digest(original);
        ClipboardHolder holder;
        QVERIFY(holder.start(tmp_.path()));
        QTRY_VERIFY_WITH_TIMEOUT(holder.ready(), 5000);
        clip::ReplayBuffer buffer;
        clip::ExportQueue queue(&buffer, nullptr, tmp_.filePath("clipboard-jobs"));
        auto id = queue.submitCompression(original, "libx264");
        QTRY_VERIFY_WITH_TIMEOUT(!queue.jobs().isEmpty() && queue.jobs().first().terminal, 30000);
        auto job = queue.jobs().first();
        QCOMPARE(job.state, QString("Compressed but clipboard copy failed"));
        QVERIFY(job.details["copyPending"].toBool());
        QVERIFY(QFileInfo::exists(job.output));
        QCOMPARE(digest(original), before);
        auto xml = clip::notificationXml(job);
        QVERIFY(xml.contains("Compressed, but could not copy."));
        QVERIFY(xml.contains("Retry Copy"));
        QVERIFY(!xml.contains("Delete unreduced"));
        holder.release();
        QString error;
        QVERIFY2(queue.retryCopy(id, &error), qPrintable(error));
        job = queue.jobs().first();
        QCOMPARE(job.state, QString("Complete"));
        QVERIFY(job.details["copied"].toBool());
        QVERIFY(!job.details["copyPending"].toBool());
        QVERIFY(clip::notificationXml(job).contains("Delete unreduced file?"));
        QCOMPARE(digest(original), before);
    }
    void shortClipboardContentionRetries() {
        ClipboardHolder holder;
        QVERIFY(holder.start(tmp_.path()));
        QTRY_VERIFY_WITH_TIMEOUT(holder.ready(), 5000);
        std::atomic_bool done = false, copied = false;
        std::jthread sender([&] {
            copied = clip::copyFile(source_);
            done = true;
        });
        QTimer::singleShot(100, this, [&] { holder.release(); });
        QTRY_VERIFY_WITH_TIMEOUT(done.load(), 5000);
        QVERIFY(copied.load());
        QVERIFY(QFileInfo::exists(source_));
    }
};
QTEST_GUILESS_MAIN(CompressionTests)
#include "compression_tests.moc"
