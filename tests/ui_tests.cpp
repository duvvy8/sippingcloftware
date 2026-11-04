#include "ui.h"
#include "editor.h"
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
        QVERIFY(!window_->toolbar_->compress_->isEnabled());
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
    void editorExportAndDecoderCleanup() {
        auto test = Registry::testEncoder("libx264");
        QVERIFY(test["encoded"].toBool());
        for (auto &cap : window_->registry_.encoders)
            if (cap.name == "libx264") {
                cap.encoded = true;
                cap.initialized = true;
                cap.tested.append(test);
            }
        window_->showEditor(1);
        auto editors = window_->findChildren<EditorDialog *>();
        QCOMPARE(editors.size(), 1);
        auto *editor = editors.first();
        editor->start_->setValue(.333);
        editor->end_->setValue(1.333);
        editor->crop_->setChecked(true);
        editor->x_->setValue(20);
        editor->y_->setValue(10);
        editor->w_->setValue(100);
        editor->h_->setValue(100);
        editor->aspect_->setCurrentText("9:16");
        QVERIFY(editor->result_->text().contains("1.000 s"));
        QVERIFY(editor->grab().save("test-output/editor-" + qEnvironmentVariable("QT_SCALE_FACTOR", "1") +
                                    ".png"));
        auto buttons = editor->findChildren<QPushButton *>();
        QPushButton *preview = nullptr;
        QPushButton *exportButton = nullptr;
        for (auto *button : buttons) {
            if (button->text() == "Preview / Stop")
                preview = button;
            if (button->text() == "Export New File")
                exportButton = button;
        }
        QVERIFY(preview && exportButton);
        QTest::mouseClick(preview, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(editor->player_ && editor->player_->duration() > 0, 10000);
        QPointer<QMediaPlayer> player(editor->player_);
        QPointer<EditorDialog> tracked(editor);
        QTest::mouseClick(exportButton, Qt::LeftButton);
        QTRY_VERIFY(tracked.isNull());
        QVERIFY(player.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(
            !window_->exports_.jobs().isEmpty() && window_->exports_.jobs().last().terminal, 15000);
        auto job = window_->exports_.jobs().last();
        QCOMPARE(job.state, QString("Complete"));
        auto edited = inspect(job.output);
        QCOMPARE(edited.width, 100);
        QCOMPARE(edited.height, 180);
        QVERIFY(std::abs(edited.duration - 1.) < .06);
        QVERIFY(QFileInfo::exists(sample_));
    }
    void libraryCompressionAction() {
        window_->selectedPath_ = sample_;
        QPushButton *compress = nullptr;
        for (auto *button : window_->tabs_->widget(0)->findChildren<QPushButton *>())
            if (button->text() == "Auto Compress")
                compress = button;
        QVERIFY(compress);
        QTest::mouseClick(compress, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(window_->exports_.jobs().last().kind == "compress" &&
                                     window_->exports_.jobs().last().terminal,
                                 15000);
        const auto job = window_->exports_.jobs().last();
        QCOMPARE(job.state, QString("Already within limit"));
        QCOMPARE(job.output, sample_);
        QVERIFY(job.details["copied"].toBool());
    }
    void inAppCompressionDecision() {
        auto original = QFileInfo(sample_).dir().filePath("large.mp4");
        QVERIFY(QFile::copy(sample_, original));
        QFile f(original);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.resize(SizeLimit + 1));
        f.close();
        auto identity = fileIdentity(original);
        auto id = window_->exports_.submitCompression(original, "libx264");
        QTRY_VERIFY_WITH_TIMEOUT(
            window_->exports_.jobs().last().id == id && window_->exports_.jobs().last().terminal, 30000);
        auto job = window_->exports_.jobs().last();
        QVERIFY2(job.state == "Complete", qPrintable(job.message));
        QTRY_VERIFY(window_->jobs_->count() == window_->exports_.jobs().size());
        for (int i = 0; i < window_->jobs_->count(); ++i)
            if (window_->jobs_->item(i)->data(Qt::UserRole).toString() == id)
                window_->jobs_->setCurrentRow(i);
        auto *yes = window_->findChild<QPushButton *>("recycleOriginal");
        auto *no = window_->findChild<QPushButton *>("retainOriginal");
        QVERIFY(yes && no && yes->isEnabled() && no->isEnabled());
        QTest::mouseClick(no, Qt::LeftButton);
        QTRY_COMPARE(window_->exports_.jobs().last().details["deleteDecision"].toString(),
                     QString("Retained"));
        QVERIFY(!yes->isEnabled() && !no->isEnabled());
        QCOMPARE(fileIdentity(original), identity);
        QVERIFY(QFileInfo::exists(job.output));
    }
    void externalRenameRecovery() {
        auto original = fileIdentity(sample_);
        QString renamed = QFileInfo(sample_).dir().filePath("renamed.mp4");
        QVERIFY(QFile::rename(sample_, renamed));
        QCOMPARE(fileIdentity(renamed), original);
        window_->refreshLibrary();
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (auto &r : window_->library_.records())
                    if (r.path == renamed)
                        return true;
                return false;
            }(),
            10000);
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
