#include "jobs.h"
#include "platform.h"
#include "hdr.h"
#include "replay_storage.h"
#include "registry.h"
#include "replay.h"
#include <QtTest>
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void filenames() {
        QCOMPARE(clip::safeName("CON"), QString("_CON"));
        QCOMPARE(clip::safeName("a/b:foo. "), QString("a_b_foo"));
        QCOMPARE(clip::safeName("日本語"), QString("日本語"));
    }
    void atomicJson() {
        QTemporaryDir d;
        const auto p = d.filePath("nested/state.json");
        QVERIFY(clip::writeJson(p, {{"ok", true}}));
        QVERIFY(clip::readJson(p)["ok"].toBool());
    }
    void clocks() {
        auto t = clip::clockSeconds();
        QTest::qSleep(10);
        QVERIFY(clip::clockSeconds() > t);
    }
    void catalogue() {
        clip::Registry r;
        r.discover();
        QVERIFY(r.encoders.size() > 20);
        QVERIFY(!r.adapters.isEmpty());
        QVERIFY(r.find("libx264"));
        QVERIFY(!r.find("png"));
        QVERIFY(!r.find("mjpeg"));
        QVERIFY(!r.find("exr"));
        QVERIFY(!r.find("pbm"));
        QVERIFY(!r.find("vnull"));
        QVERIFY(r.find("v210"));
        QVERIFY(!r.find("v210")->replaySuitable());
        QVERIFY(r.find("ffv1"));
        QVERIFY(!r.find("ffv1")->initialized);
        QVERIFY(r.automatic().isEmpty());
    }
    void softwareEncode() {
        auto result = clip::Registry::testEncoder("libx264");
        QVERIFY2(result["encoded"].toBool(), qPrintable(result["reason"].toString()));
        QVERIFY(result["packets"].toInt() >= 12);
    }
    void processFailure() {
        auto p = clip::runProcess("C:/no-such-program.exe", {}, 2000);
        QVERIFY(p.code != 0);
    }
    void ringPins() {
        QTemporaryDir d;
        clip::ReplayBuffer b;
        b.begin(100, 5, 1000);
        auto add = [&](int index, double begin, double end) {
            auto path = d.filePath(QString::number(index));
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(100, 'x'));
            f.close();
            b.add({path, begin, end, 100});
        };
        add(1, 100, 102);
        auto p = b.pin(102, 30);
        QCOMPARE(p.duration(), 2.);
        add(2, 102, 104);
        add(3, 104, 106);
        add(4, 106, 108);
        QVERIFY(QFile::exists(d.filePath("1")));
        auto interval = b.resolve(p.id);
        QCOMPARE(interval.end, 102.);
        QCOMPARE(interval.segments.size(), 1);
        b.release(p.id);
        QVERIFY(!QFile::exists(d.filePath("1")));
        b.stop();
        b.clear();
    }
    void pendingEndpoint() {
        QTemporaryDir d;
        clip::ReplayBuffer b;
        b.begin(0, 300, 1000);
        auto p = b.pin(2.5, 10);
        auto p2 = b.pin(2.0, 1);
        std::thread producer([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            QFile f(d.filePath("a"));
            f.open(QIODevice::WriteOnly);
            f.write("x");
            f.close();
            b.add({f.fileName(), 0, 3, 1});
        });
        auto resolved = b.resolve(p.id);
        producer.join();
        QCOMPARE(resolved.end, 2.5);
        QCOMPARE(resolved.duration(), 2.5);
        QCOMPARE(b.resolve(p2.id).duration(), 1.);
        b.release(p.id);
        b.release(p2.id);
        b.stop();
        b.clear();
    }
    void ringByteBound() {
        QTemporaryDir d;
        clip::ReplayBuffer b;
        b.begin(0, 300, 100);
        for (int i = 0; i < 4; ++i) {
            QFile f(d.filePath(QString::number(i)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(60, 'x'));
            f.close();
            b.add({f.fileName(), double(i), double(i + 1), 60});
        }
        QCOMPARE(b.count(), 1);
        QCOMPARE(b.bytes(), 60);
        b.stop();
        b.clear();
    }
    void shortcutConflictPreservesBinding() {
        QString error;
        constexpr UINT modifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
        QVERIFY(RegisterHotKey(nullptr, 45000, modifiers, VK_F23));
        {
            clip::Hotkeys keys;
            QVERIFY2(keys.assign("record", "Ctrl+Alt+Shift+F24", &error), qPrintable(error));
            QVERIFY(!keys.assign("record", "Ctrl+Alt+Shift+F23", &error));
            QVERIFY(!RegisterHotKey(nullptr, 45001, modifiers, VK_F24));
        }
        QVERIFY(RegisterHotKey(nullptr, 45001, modifiers, VK_F24));
        QVERIFY(UnregisterHotKey(nullptr, 45001));
        QVERIFY(UnregisterHotKey(nullptr, 45000));
    }
    void interruptedJobRetainsOriginal() {
        QTemporaryDir dir;
        clip::ExportJob job;
        job.id = "interrupted-fixture";
        job.state = "Validating";
        job.original = dir.filePath("original.mp4");
        QFile original(job.original);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("preserve"), qint64(8));
        original.close();
        QVERIFY(clip::writeJson(dir.filePath("jobs/" + job.id + ".json"), job.json()));
        clip::ReplayBuffer buffer;
        clip::ExportQueue queue(&buffer, nullptr, dir.filePath("jobs"));
        QCOMPARE(queue.jobs().size(), 1);
        QCOMPARE(queue.jobs().first().state, QString("Failed"));
        QVERIFY(queue.jobs().first().terminal);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("preserve"));
    }
    void hdrTransferAndFloatConversion() {
        QCOMPARE(clip::halfFloat(0x3c00), 1.f);
        QCOMPARE(clip::halfFloat(0x4a40), 12.5f);
        QCOMPARE(clip::halfFloat(0xbc00), -1.f);
        QVERIFY(std::abs(clip::halfFloat(1) - std::pow(2.f, -24)) < 1e-12);
        for (double nits : {.001, 1., 80., 1000., 10000.})
            QVERIFY(std::abs(clip::pqDecode(clip::pqEncode(nits)) - nits) < .0001);
        clip::CapturedFrame source;
        source.width = source.height = 4;
        source.stride = 32;
        source.floating = true;
        source.pixels.resize(128);
        auto *values = reinterpret_cast<quint16 *>(source.pixels.data());
        for (int i = 0; i < 64; ++i)
            values[i] = 0x3c00;
        auto *f = av_frame_alloc();
        f->width = f->height = 4;
        f->format = AV_PIX_FMT_P010LE;
        clip::check(av_frame_get_buffer(f, 32), "Test HDR buffer");
        clip::convertFloatFrame(source, f, true);
        QCOMPARE(int(reinterpret_cast<quint16 *>(f->data[0])[0] >> 6), qRound(64 + 876 * clip::pqEncode(80)));
        QCOMPARE(int(reinterpret_cast<quint16 *>(f->data[1])[0] >> 6), 512);
        QCOMPARE(f->color_trc, AVCOL_TRC_SMPTE2084);
        QCOMPARE(f->color_primaries, AVCOL_PRI_BT2020);
        av_frame_free(&f);
    }
    void hdrEncoderDepthVerification() {
        auto hdr = clip::Registry::testEncoder("libx265", true);
        QVERIFY2(hdr["encoded"].toBool(), qPrintable(hdr["reason"].toString()));
        QVERIFY(hdr["decodedDepth"].toInt() >= 10);
        auto unsupported = clip::Registry::testEncoder("mpeg1video", true);
        QVERIFY(!unsupported["encoded"].toBool());
    }
    void activeBudgetAndContiguousDuration() {
        QTemporaryDir dir;
        clip::ReplayBuffer b;
        b.begin(0, 10, 100);
        auto add = [&](int index, double start, double end) {
            auto path = dir.filePath(QString::number(index));
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(QByteArray(40, 'x')), qint64(40));
            f.close();
            b.add({path, start, end, 40});
        };
        add(1, 0, 2);
        auto old = b.pin(2, 2);
        add(2, 2, 4);
        add(3, 4, 6);
        QCOMPARE(b.available(6), 2.);
        auto recent = b.pin(6, 5);
        QCOMPARE(recent.duration(), 2.);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, b.reserve(30));
        QVERIFY(QFileInfo::exists(dir.filePath("1")));
        QVERIFY(QFileInfo::exists(dir.filePath("3")));
        b.reserve(0);
        b.release(old.id);
        b.release(recent.id);
        b.stop();
        b.clear();
    }
    void unavailableEndpointIsNotShortened() {
        QTemporaryDir dir;
        auto path = dir.filePath("segment");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("frame");
        f.close();
        clip::ReplayBuffer b;
        b.begin(0, 300, 100);
        b.add({path, 0, 2, 5});
        auto pin = b.pin(2.5, 3);
        b.stop();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, b.resolve(pin.id));
        QVERIFY(QFileInfo::exists(path));
        b.release(pin.id);
        b.clear();
    }
    void replaySessionRecoveryAndIdentityProtection() {
        QTemporaryDir dir;
        auto root = dir.filePath("buffer");
        QString active, session, renamed;
        {
            clip::ReplayStorage storage;
            storage.begin(root, "fixture-owner");
            active = storage.reserve();
            session = QFileInfo(active).absolutePath();
            QFile f(active);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("header");
            f.close();
            storage.identifyActive();
            QVERIFY(f.open(QIODevice::Append));
            f.write("delayed packets");
            f.close();
            renamed = active.chopped(8);
            QVERIFY(QFile::rename(active, renamed));
        }
        auto journal = clip::readJson(QDir(session).filePath("session.json"));
        journal["pid"] = 0;
        QVERIFY(clip::writeJson(QDir(session).filePath("session.json"), journal));
        {
            clip::ReplayStorage recovered;
            recovered.begin(root, "new-owner");
            QVERIFY(!QFileInfo::exists(renamed));
            auto partial = recovered.reserve();
            QFile f(partial);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("buffer");
            f.close();
            recovered.identifyActive();
            auto final = partial.chopped(8);
            QVERIFY(QFile::rename(partial, final));
            clip::Segment segment{final, 0, 2, 6, clip::fileIdentity(final)};
            recovered.committed({segment});
            recovered.stop();
            session = QFileInfo(final).absolutePath();
            QVERIFY(QFile::remove(final));
            QFile replacement(final);
            QVERIFY(replacement.open(QIODevice::WriteOnly));
            replacement.write("original recording placed here");
            replacement.close();
            renamed = final;
        }
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, clip::ReplayStorage::recover(root, "new-owner"));
        QFile retained(renamed);
        QVERIFY(retained.open(QIODevice::ReadOnly));
        QCOMPARE(retained.readAll(), QByteArray("original recording placed here"));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
