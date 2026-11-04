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
    QString expectedDisplay_;
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
        Registry hardware;
        hardware.discover();
        QVERIFY(!hardware.displays.isEmpty());
        expectedDisplay_ = hardware.displays.last().identity;
        settings.monitorIdentity = expectedDisplay_;
        settings.monitor = 0; // Deliberately stale enumeration index; identity must win.
        QVERIFY(settings.save());
        window_ = std::make_unique<MainWindow>();
        window_->show();
        QTest::qWait(100);
    }
    void navigationAndFonts() {
        QCOMPARE(window_->readRecording().monitorIdentity, expectedDisplay_);
        QCOMPARE(window_->windows_->currentData().toULongLong(), qulonglong(0));
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
    void realWindowCaptureToolbarSaveAndSourceClosure() {
        auto fixture = std::make_unique<QLabel>("Live capture verification");
        fixture->setWindowTitle("Capture verification");
        fixture->setMinimumSize(640, 360);
        fixture->setAlignment(Qt::AlignCenter);
        fixture->show();
        QVERIFY(QTest::qWaitForWindowExposed(fixture.get()));
        QTimer movement;
        int frame = 0;
        connect(&movement, &QTimer::timeout, fixture.get(),
                [&] { fixture->setText(QString("HUD 0123456789 · frame %1").arg(++frame)); });
        movement.start(16);
        auto settings = window_->readRecording();
        settings.source = "window";
        settings.window = quintptr(fixture->winId());
        settings.windowExecutable.clear();
        settings.windowProcessCreated.clear();
        settings.windowPid = 0;
        settings.encoder = "libx264";
        settings.fps = 30;
        settings.height = 360;
        settings.backend = "wgc";
        settings.hdr = false;
        settings.audio.clear();
        settings.bufferFolder = temporary_.filePath("real-capture-buffer");
        HMONITOR monitor = MonitorFromWindow(HWND(settings.window), MONITOR_DEFAULTTONEAREST);
        for (int i = 0; i < window_->registry_.displays.size(); ++i)
            if (window_->registry_.displays[i].handle == quintptr(monitor)) {
                settings.monitor = i;
                settings.monitorIdentity = window_->registry_.displays[i].identity;
            }
        window_->recorded_ = settings;
        window_->recorder_.start(settings, window_->registry_);
        auto stop = qScopeGuard([&] {
            movement.stop();
            window_->recorder_.stop();
        });
        QTest::qWait(2300);
        window_->toolbar_->open();
        QVERIFY(window_->toolbar_->isVisible());
        QVERIFY(window_->toolbar_->save_->isEnabled());
        double endpoint = window_->toolbar_->endpoint_;
        QTest::qWait(300);
        QTest::mouseClick(window_->toolbar_->save_, Qt::LeftButton);
        QVERIFY(!window_->toolbar_->isVisible());
        QCOMPARE(window_->toolbar_->pin_.id, QString());
        QTest::qWait(200);
        movement.stop();
        fixture.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!window_->recorder_.active(), 5000);
        QVERIFY(window_->recorder_.stats().error.contains("closed", Qt::CaseInsensitive));
        QTRY_VERIFY_WITH_TIMEOUT(window_->exports_.jobs().last().kind == "save" &&
                                     window_->exports_.jobs().last().terminal,
                                 30000);
        auto job = window_->exports_.jobs().last();
        QVERIFY2(job.state == "Complete", qPrintable(job.message));
        QCOMPARE(job.source.end, endpoint);
        auto media = inspect(job.original);
        QVERIFY(qAbs(media.duration - job.source.duration()) <= 1. / media.fps + .001);
        QVERIFY(decodeValidate(job.original));
        QVERIFY(window_->recorder_.stats().backlog == 0);
        window_->recorder_.stop();
        window_->recorder_.buffer().clear();
    }
    void restartApprovalAndCaptureCalibration() {
        window_->populateEncoders();
        window_->encoder_->setCurrentIndex(window_->encoder_->findData("libx264"));
        window_->source_->setCurrentIndex(window_->source_->findData("monitor"));
        window_->recordFps_->setCurrentIndex(window_->recordFps_->findData(30));
        window_->recordHeight_->setCurrentIndex(window_->recordHeight_->findData(480));
        auto settings = window_->readRecording();
        window_->recorded_ = settings;
        window_->recorder_.start(settings, window_->registry_);
        auto stop = qScopeGuard([&] { window_->recorder_.stop(); });
        QTest::qWait(250);
        auto initial = window_->recorder_.stats();
        window_->recordFps_->setCurrentIndex(window_->recordFps_->findData(15));
        QTimer response;
        response.setInterval(10);
        bool asked = false;
        QString question;
        QMessageBox::StandardButton answer = QMessageBox::No;
        connect(&response, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                asked = true;
                question = box->text();
                response.stop();
                if (auto *button = box->button(answer))
                    QTest::mouseClick(button, Qt::LeftButton);
                else
                    box->reject();
            }
        });
        response.start();
        window_->applyRecording();
        response.stop();
        QVERIFY(asked && question.contains("clears the replay buffer"));
        QVERIFY(window_->recorder_.active());
        QCOMPARE(window_->recorder_.stats().fps, 30);
        QCOMPARE(window_->recorder_.stats().started, initial.started);
        asked = false;
        answer = QMessageBox::Yes;
        response.start();
        window_->applyRecording();
        response.stop();
        QVERIFY(asked && question.contains("clears the replay buffer"));
        QVERIFY(window_->recorder_.active());
        QCOMPARE(window_->recorder_.stats().fps, 15);
        QVERIFY(window_->recorder_.stats().started > initial.started);
        window_->toggleRecording();
        QVERIFY(!window_->recorder_.active());
        window_->recorder_.buffer().clear();
        window_->calibrate();
        QTRY_VERIFY_WITH_TIMEOUT(!window_->discovery_, 30000);
        const auto *cap = window_->registry_.find("libx264");
        QVERIFY(cap && cap->live);
        bool matched = false;
        for (auto value : cap->tested)
            matched |=
                value.toObject()["sourceKey"].toString() == captureProfileKey(window_->readRecording());
        QVERIFY(matched);
        window_->encoder_->setCurrentIndex(window_->encoder_->findData("auto"));
        window_->toggleRecording();
        QVERIFY(window_->recorder_.active());
        QCOMPARE(window_->recorder_.stats().encoder, QString("libx264"));
        QCOMPARE(window_->recorder_.stats().fps, 15);
        QTest::qWait(150);
        window_->toggleRecording();
        window_->recorder_.buffer().clear();
        window_->recordFps_->setCurrentIndex(window_->recordFps_->findData(30));
        asked = false;
        answer = QMessageBox::Ok;
        response.start();
        window_->toggleRecording();
        response.stop();
        QVERIFY(asked && question.contains("profile changed"));
        QVERIFY(!window_->recorder_.active());
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
    void missingSourcesDoNotSelectReplacements() {
        window_.reset();
        Settings settings;
        settings.load();
        settings.monitorIdentity = "missing-monitor-interface";
        settings.source = "window";
        settings.windowExecutable = "C:/Previous/game.exe";
        settings.autoRecord = true;
        QVERIFY(settings.save());
        window_ = std::make_unique<MainWindow>(true);
        QCOMPARE(window_->display_->currentData().toInt(), -1);
        QCOMPARE(window_->windows_->currentData().toULongLong(), qulonglong(0));
        QVERIFY(!window_->recorder_.active());
        window_->refreshCaptureSources();
        QCOMPARE(window_->display_->currentData().toInt(), -1);
        QCOMPARE(window_->windows_->currentData().toULongLong(), qulonglong(0));
    }
    void cleanupTestCase() {
        window_.reset();
    }
};
} // namespace clip
QTEST_MAIN(clip::UiTests)
#include "ui_tests.moc"
