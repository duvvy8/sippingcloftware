#include "edit.h"
#include "jobs.h"
#include "hdr.h"
#include <QtTest>
#include <cmath>
class MediaTests : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    QString original_;
    QByteArray digest_;
    QByteArray digest(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        QCryptographicHash h(QCryptographicHash::Sha256);
        h.addData(&f);
        return h.result();
    }
    double rms(const QString &path, int track, double start = 0, double duration = 0) {
        QStringList args{"-v", "error", "-ss", QString::number(start), "-i", path};
        if (duration > 0)
            args << "-t" << QString::number(duration);
        args << "-map" << QString("0:a:%1").arg(track) << "-ac" << "1" << "-ar" << "48000" << "-f" << "f32le"
             << "pipe:1";
        auto p = clip::runProcess(clip::ffmpegPath(), args, 30000);
        if (p.code != 0)
            return -1;
        double sum = 0;
        int count = p.output.size() / 4;
        for (int i = 0; i < count; ++i) {
            float v;
            memcpy(&v, p.output.constData() + i * 4, 4);
            sum += double(v) * v;
        }
        return count ? std::sqrt(sum / count) : 0;
    }
  private slots:
    void initTestCase() {
        qputenv("CLIPPER_STATE_DIR", directory_.filePath("state").toUtf8());
        original_ = directory_.filePath("original.mp4");
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
                                   "-f",
                                   "lavfi",
                                   "-i",
                                   "sine=frequency=880:sample_rate=48000",
                                   "-t",
                                   "12",
                                   "-map",
                                   "0:v",
                                   "-map",
                                   "1:a",
                                   "-map",
                                   "2:a",
                                   "-c:v",
                                   "libx264",
                                   "-preset",
                                   "ultrafast",
                                   "-g",
                                   "60",
                                   "-keyint_min",
                                   "60",
                                   "-sc_threshold",
                                   "0",
                                   "-bf",
                                   "2",
                                   "-c:a",
                                   "aac",
                                   original_},
                                  30000);
        QVERIFY2(p.code == 0, p.output.constData());
        digest_ = digest(original_);
    }
    void exactTrimAndAudio() {
        clip::EditRequest r;
        r.start = 2.3;
        r.end = 6.7;
        r.tracks = {1};
        r.gains = {0.5};
        auto out = directory_.filePath("exact.mp4");
        auto m = clip::editMedia(original_, out, r, "libx264", "High");
        QVERIFY(std::abs(m.duration - 4.4) < 1. / 30. + m.audioRounding + .001);
        QCOMPARE(m.audioTracks, 1);
        QVERIFY(clip::decodeValidate(out));
        double reference = rms(original_, 1, 2.3, 4.4), changed = rms(out, 0);
        QVERIFY(reference > 0);
        QVERIFY2(std::abs(changed / reference - .5) < .06, qPrintable(QString::number(changed / reference)));
        QCOMPARE(digest(original_), digest_);
    }
    void fastTrimRoundsAndDecodes() {
        auto bounds = clip::trimBounds(original_, 2.3, 6.7, true);
        QCOMPARE(bounds.start, 2.);
        QCOMPARE(bounds.end, 8.);
        QVERIFY(bounds.rounded);
        clip::EditRequest r;
        r.start = 2.3;
        r.end = 6.7;
        r.fast = true;
        r.tracks = {0, 1};
        r.gains = {1, 1};
        auto out = directory_.filePath("fast.mp4");
        auto m = clip::editMedia(original_, out, r, "libx264", "High");
        QVERIFY(std::abs(m.duration - 6.) < 1. / 30. + m.audioRounding + .001);
        QCOMPARE(m.audioTracks, 2);
        QVERIFY(clip::decodeValidate(out));
        QCOMPARE(digest(original_), digest_);
    }
    void visibleCropAndPortraitFit() {
        clip::EditRequest r;
        r.start = 0;
        r.end = 1;
        r.crop = true;
        r.region = {10, 20, 160, 90};
        r.tracks = {0, 1};
        r.gains = {1, 1};
        auto m = clip::editMedia(original_, directory_.filePath("crop.mp4"), r, "libx264", "High");
        QCOMPARE(m.width, 160);
        QCOMPARE(m.height, 90);
        r.crop = false;
        r.aspect = "9:16";
        auto out = directory_.filePath("portrait.mp4");
        m = clip::editMedia(original_, out, r, "libx264", "High");
        QCOMPARE(m.width, 100);
        QCOMPARE(m.height, 180);
        auto image = clip::thumbnail(out, .5);
        QVERIFY(!image.isNull());
        auto top = image.pixelColor(image.width() / 2, 2);
        QVERIFY(top.red() < 15 && top.green() < 15 && top.blue() < 15);
        QCOMPARE(digest(original_), digest_);
    }
    void mixingAndMuting() {
        clip::EditRequest r;
        r.start = 0;
        r.end = 1;
        r.tracks = {0, 1};
        r.gains = {0, 0};
        r.mixAudio = true;
        auto out = directory_.filePath("muted.mp4");
        auto m = clip::editMedia(original_, out, r, "libx264", "High");
        QCOMPARE(m.audioTracks, 1);
        QVERIFY(rms(out, 0) < 0.00001);
        QCOMPARE(digest(original_), digest_);
    }
    void cancellationKeepsOriginal() {
        clip::EditRequest r;
        r.start = 0;
        r.end = 12;
        r.tracks = {0, 1};
        r.gains = {1, 1};
        std::atomic_bool cancel = true;
        auto out = directory_.filePath("cancelled.mp4");
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 clip::editMedia(original_, out, r, "libx264", "High", &cancel));
        QVERIFY(!QFileInfo::exists(out));
        QVERIFY(!QFileInfo::exists(out + ".partial"));
        QCOMPARE(digest(original_), digest_);
    }
    void queuedIntervalsAreImmutable() {
        auto bufferSource = directory_.filePath("buffer-source.mp4");
        QVERIFY(QFile::copy(original_, bufferSource));
        clip::ReplayBuffer buffer;
        buffer.begin(0, 300, 1024 * 1024 * 1024);
        buffer.add({bufferSource, 0, 12, QFileInfo(bufferSource).size()});
        auto a = buffer.pin(4, 2), b = buffer.pin(8, 3);
        clip::Settings s;
        s.library = directory_.filePath("saved");
        s.encoder = "libx264";
        s.fps = 30;
        s.height = 0;
        s.container = "mp4";
        clip::ExportQueue queue(&buffer, nullptr, directory_.filePath("jobs"));
        auto first = queue.submitSave(a, s, "Fixture", "");
        auto second = queue.submitSave(b, s, "Fixture", "");
        Q_UNUSED(first);
        Q_UNUSED(second);
        s.encoder = "unavailable";
        s.fps = 15;
        QTRY_VERIFY_WITH_TIMEOUT(
            queue.jobs().size() == 2 && queue.jobs()[0].terminal && queue.jobs()[1].terminal, 30000);
        auto jobs = queue.jobs();
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(jobs[i].state, QString("Complete"));
            auto media = clip::inspect(jobs[i].original);
            QVERIFY(std::abs(media.duration - (i ? 3. : 2.)) < .06);
            QCOMPARE(media.fps, 30.);
            QCOMPARE(media.audioTracks, 2);
            QVERIFY(clip::decodeValidate(jobs[i].original));
        }
        queue.shutdown();
        buffer.stop();
        buffer.clear();
        QCOMPARE(digest(original_), digest_);
    }
    void hdrPreservationAndExplicitSdr() {
        auto hdr = directory_.filePath("hdr.mkv");
        auto generated = clip::runProcess(
            clip::ffmpegPath(),
            {"-v",
             "error",
             "-y",
             "-i",
             original_,
             "-t",
             "1",
             "-map",
             "0:v",
             "-an",
             "-vf",
             "zscale=tin=709:min=709:pin=709:rin=limited:t=smpte2084:p=2020:m=2020_ncl:r=limited",
             "-pix_fmt",
             "yuv420p10le",
             "-c:v",
             "libx265",
             "-preset",
             "ultrafast",
             "-x265-params",
             "master-display=G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1):max-cll="
             "1000,400:repeat-headers=1:pools=1:frame-threads=1",
             "-color_primaries",
             "bt2020",
             "-color_trc",
             "smpte2084",
             "-colorspace",
             "bt2020nc",
             "-color_range",
             "tv",
             hdr},
            30000);
        QVERIFY2(generated.code == 0, generated.output.constData());
        auto source = clip::inspect(hdr);
        QVERIFY(source.hdr);
        QVERIFY(source.hdrMetadata.contains("mastering"));
        QVERIFY(source.hdrMetadata.contains("light"));
        auto savedDigest = digest(hdr);
        clip::EditRequest request;
        request.start = 0;
        request.end = 1;
        auto preserved =
            clip::editMedia(hdr, directory_.filePath("preserved.mkv"), request, "libx265", "High");
        QVERIFY(preserved.hdr);
        QCOMPARE(preserved.primaries, source.primaries);
        QCOMPARE(preserved.transfer, source.transfer);
        QCOMPARE(preserved.hdrMetadata, source.hdrMetadata);
        QVERIFY(preserved.pixelFormat.contains("10"));
        request.sdr = true;
        auto sdr = clip::editMedia(hdr, directory_.filePath("sdr.mp4"), request, "libx264", "High");
        QVERIFY(!sdr.hdr);
        QCOMPARE(sdr.primaries, QString("bt709"));
        QCOMPARE(sdr.transfer, QString("bt709"));
        QCOMPARE(sdr.matrix, QString("bt709"));
        QVERIFY(sdr.hdrMetadata.isEmpty());
        QCOMPARE(digest(hdr), savedDigest);
    }
};
QTEST_GUILESS_MAIN(MediaTests)
#include "media_tests.moc"
