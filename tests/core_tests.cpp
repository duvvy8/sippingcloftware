#include "jobs.h"
#include "platform.h"
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
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
