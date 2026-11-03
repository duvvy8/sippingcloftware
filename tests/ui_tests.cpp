#include "ui.h"
#include <QtTest>
#include <shellapi.h>
namespace clip {
class UiTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_;
    std::unique_ptr<MainWindow> window_;
    QString sample_;
  private slots:
    void initTestCase() {
        qputenv("CLIPPER_STATE_DIR", temporary_.path().toUtf8());
        applyAppearance(*qobject_cast<QApplication *>(QCoreApplication::instance()));
        QApplication::setQuitOnLastWindowClosed(false);
        Settings settings;
        settings.library = temporary_.filePath("clips");
        settings.bufferFolder = temporary_.filePath("buffer");
        settings.overlayHotkey = "Ctrl+Alt+Shift+F21";
        settings.recordHotkey = "Ctrl+Alt+Shift+F22";
        QVERIFY(settings.save());
        window_ = std::make_unique<MainWindow>();
        window_->show();
        QTest::qWait(100);
    }
    void navigationAndFonts() {
        QVERIFY(QFontInfo(QApplication::font()).family().contains("Inter"));
        QCOMPARE(window_->tabs_->count(), 3);
        for (int i = 0; i < 3; ++i) {
            auto *bar = window_->tabs_->tabBar();
            QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(i).center());
            QCOMPARE(window_->tabs_->currentIndex(), i);
        }
        QCOMPARE(window_->recordStats_->font().family(), QString("Geist Mono"));
        window_->tabs_->setCurrentIndex(0);
        QDir().mkpath("test-output");
        const auto scale = qEnvironmentVariable("QT_SCALE_FACTOR", "1");
        QVERIFY(window_->grab().save("test-output/ui-" + scale + ".png"));
    }
    void toolbarDismissAndPartialState() {
        window_->toolbar_->open();
        QVERIFY(window_->toolbar_->isVisible());
        QVERIFY(!window_->toolbar_->save_->isEnabled());
        QTest::keyClick(window_->toolbar_, Qt::Key_Escape);
        QVERIFY(!window_->toolbar_->isVisible());
        QCOMPARE(window_->toolbar_->pin_.id, QString());
    }
    void libraryPlaybackAndClipboard() {
        sample_ = QDir(window_->settings_.library).filePath("Fixture/sample.mp4");
        QDir().mkpath(QFileInfo(sample_).absolutePath());
        auto result = runProcess(ffmpegPath(),
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
                                  "2",
                                  "-c:v",
                                  "libx264",
                                  "-preset",
                                  "ultrafast",
                                  "-c:a",
                                  "aac",
                                  sample_},
                                 30000);
        QVERIFY2(result.code == 0, result.output.constData());
        window_->refreshLibrary();
        QTRY_COMPARE_WITH_TIMEOUT(window_->clips_->rowCount(), 1, 10000);
        window_->clips_->selectRow(0);
        QCOMPARE(window_->selectedPath_, sample_);
        QTRY_VERIFY_WITH_TIMEOUT(!window_->preview_->pixmap().isNull(), 10000);
        QString error;
        QVERIFY2(copyFile(sample_, &error), qPrintable(error));
        QVERIFY(OpenClipboard(nullptr));
        QVERIFY(IsClipboardFormatAvailable(CF_HDROP));
        auto data = GetClipboardData(CF_HDROP);
        wchar_t name[32768]{};
        QVERIFY(DragQueryFileW(HDROP(data), 0, name, 32768) > 0);
        CloseClipboard();
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(name)), sample_);
        window_->showPlayer(sample_);
        auto players = window_->findChildren<QMediaPlayer *>();
        QCOMPARE(players.size(), 1);
        auto *player = players.first();
        QTRY_VERIFY_WITH_TIMEOUT(player->duration() > 0, 10000);
        auto *dialog = qobject_cast<QDialog *>(player->parent());
        QVERIFY(dialog);
        QPointer<QMediaPlayer> tracked(player);
        dialog->close();
        QTRY_VERIFY(tracked.isNull());
    }
    void externalRenameRecovery() {
        auto original = fileIdentity(sample_);
        QString renamed = QFileInfo(sample_).dir().filePath("renamed.mp4");
        QVERIFY(QFile::rename(sample_, renamed));
        QCOMPARE(fileIdentity(renamed), original);
        window_->refreshLibrary();
        QTRY_COMPARE_WITH_TIMEOUT(window_->library_.records().size(), 1, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(window_->library_.records().first().path, renamed, 10000);
    }
    void closeHidesWithoutExit() {
        window_->close();
        QVERIFY(!window_->isVisible());
        QVERIFY(!window_->recorder_.active());
    }
    void cleanupTestCase() {
        window_.reset();
    }
};
} // namespace clip
QTEST_MAIN(clip::UiTests)
#include "ui_tests.moc"
