#include <QtTest>

#include <QLabel>
#include <QStandardPaths>

#include <QRadioButton>
#include <QSettings>

#include "TestGguf.h"
#include "core/ModelCatalog.h"
#include "core/ModelManager.h"
#include "ui/PreviewCanvas.h"
#include "core/Pipeline.h"
#include "core/PipelineStep.h"
#include "ui/MainWindow.h"

#include <memory>

namespace {

// Deterministic, GPU-free step so this can construct a real Pipeline
// without a real segmentation/upscale model, same approach test_pipeline.cpp
// and test_batch_runner.cpp use. Only its name matters here; MainWindow
// picks its default-enabled step by matching "Background Removal".
class NamedStep : public PipelineStep {
public:
    explicit NamedStep(QString name) : name_(std::move(name)) {}
    QImage process(const QImage& input, PipelineRun&) const override { return input; }
    QString name() const override { return name_; }

private:
    QString name_;
};

} // namespace

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void constructionLeavesDefaultStepEnabledDespiteAdvancedPagePopulation();
    void everyModelCategoryStartsWithItsDefaultSelected();
    void loadImageWiresCompareAndResetsTheView();
    void zoomStripHasAScrimSoItStaysLegibleOverAnyImage();
    void settingsPageListsScannedCustomModels();
    void defaultExportPathIsAbsoluteAndDerivedFromTheSourceName();
    void defaultExportPathFallsBackToOutputWithoutASource();
};

void TestMainWindow::constructionLeavesDefaultStepEnabledDespiteAdvancedPagePopulation() {
    auto pipeline = std::make_shared<Pipeline>();
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Background Removal")));
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Upscaling")));

    MainWindow window(pipeline);

    // Regression test: populateAdvancedStepList() once let
    // QListWidget::itemChanged fire during construction, which wrote the
    // items' transient Unchecked state back into Pipeline and silently
    // disabled Background Removal. The builder blocks that signal.
    QVERIFY(pipeline->isStepEnabled(0));
    QVERIFY(!pipeline->isStepEnabled(1));
}

void TestMainWindow::initTestCase() {
    // Redirect AppDataLocation so QSettings and ModelManager stay out of the
    // developer's real configuration.
    QStandardPaths::setTestModeEnabled(true);
}

void TestMainWindow::everyModelCategoryStartsWithItsDefaultSelected() {
    // Regression test: the Settings dialog used to open with no Denoise model
    // selected. MainWindow only seeded Segmentation and Upscale from
    // QSettings; the Denoise category (added later) was missed, so its radios
    // all started unchecked and the category had no visible active model.
    QSettings settings;
    settings.remove(QStringLiteral("models/segmentationModel"));
    settings.remove(QStringLiteral("models/denoiseModel"));
    settings.remove(QStringLiteral("models/upscaleModel"));

    auto pipeline = std::make_shared<Pipeline>();
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Background Removal")));
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Denoise")));
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Upscale")));

    ModelManager manager(QString()); // empty build defaults dir; fine for tests
    MainWindow window(pipeline, std::make_shared<ModelManager>(QString()));

    const auto radios = window.findChildren<QRadioButton*>();
    QVERIFY(!radios.isEmpty());
    for (const ModelInfo& info : ModelCatalog::allModels()) {
        QRadioButton* radio = nullptr;
        for (QRadioButton* candidate : radios) {
            if (candidate->text() == info.displayName) {
                radio = candidate;
                break;
            }
        }
        QVERIFY2(radio, qPrintable(QStringLiteral("radio for %1").arg(info.displayName)));

        // With settings cleared, every category's default entry must be the
        // checked one.
        const QString active =
            QSettings()
                .value(ModelCatalog::settingsKey(info.category),
                       ModelCatalog::defaultFilename(info.category))
                .toString();
        QCOMPARE(radio->isChecked(), active == info.filename);
    }
}

void TestMainWindow::loadImageWiresCompareAndResetsTheView() {
    // Integration check for the preview wiring: loading a still image feeds
    // both canvas images, enables compare, and resets the view (fit, divider
    // centered). The pipeline's steps are GPU-free named pass-throughs, so
    // the processed result equals the source.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(300, 200, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const QString path = dir.filePath(QStringLiteral("photo.png"));
    QVERIFY(image.save(path));

    auto pipeline = std::make_shared<Pipeline>();
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Background Removal")));
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Denoise")));

    MainWindow window(pipeline, std::make_shared<ModelManager>(QString()));
    window.loadImage(path);

    auto* canvas = window.findChild<PreviewCanvas*>();
    QVERIFY(canvas);

    // Simple mode reprocesses automatically: compare only becomes available
    // once the worker result lands in the canvas as the after image.
    QTRY_VERIFY(canvas->isCompareAvailable());
    QVERIFY(canvas->isWipeEnabled());
    QCOMPARE(canvas->dividerPosition(), 0.5);
    QCOMPARE(canvas->zoom(), canvas->fitZoom());
}

void TestMainWindow::zoomStripHasAScrimSoItStaysLegibleOverAnyImage() {
    // The zoom strip floats over the image; without a backdrop its flat text
    // is unreadable over bright photos. It must paint a dark scrim so it
    // stays legible regardless of what is beneath it.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(300, 200, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const QString path = dir.filePath(QStringLiteral("photo.png"));
    QVERIFY(image.save(path));

    auto pipeline = std::make_shared<Pipeline>();
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Background Removal")));

    MainWindow window(pipeline, std::make_shared<ModelManager>(QString()));
    auto* canvas = window.findChild<PreviewCanvas*>();
    QVERIFY(canvas);
    canvas->resize(400, 300);

    window.loadImage(path);
    QTRY_VERIFY(canvas->isCompareAvailable());

    // Image is 300x200 at 1:1, centered: x=50..350, y=50..250. The strip sits
    // at (8,8) over the checkerboard; (380,12) is plain checkerboard.
    const QImage rendered = canvas->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QColor inside = rendered.pixelColor(12, 12);
    const QColor outside = rendered.pixelColor(380, 12);
    if (qEnvironmentVariableIsSet("STRIP_DEBUG")) {
        for (QPoint p : {QPoint(12, 12), QPoint(30, 12), QPoint(200, 12), QPoint(380, 12)}) {
            const QColor c = rendered.pixelColor(p);
            qDebug() << p << c.name() << (c.red() + c.green() + c.blue()) / 3;
        }
        rendered.save(QStringLiteral("/tmp/strip_fail.png"));
    }
    const int insideBrightness = (inside.red() + inside.green() + inside.blue()) / 3;
    const int outsideBrightness = (outside.red() + outside.green() + outside.blue()) / 3;
    // The scrim darkens whatever is beneath (checkerboard is 160/210 gray).
    QVERIFY2(insideBrightness < 120,
             qPrintable(QStringLiteral("strip too bright: %1").arg(insideBrightness)));
    QVERIFY(outsideBrightness > insideBrightness);
}

void TestMainWindow::settingsPageListsScannedCustomModels() {
    // Folder-as-state: files in the models directory that are not catalog
    // entries become settings rows, classified by GGUF architecture.
    ModelManager writer{QString()};
    QVERIFY(TestGguf::writeTestGguf(writer.pathFor(QStringLiteral("custom-scunet.gguf")),
                                    QStringLiteral("scunet")));
    QVERIFY(TestGguf::writeTestGguf(writer.pathFor(QStringLiteral("some-rmbg.gguf")),
                                    QStringLiteral("rmbg")));

    auto pipeline = std::make_shared<Pipeline>();
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Denoise")));
    MainWindow window(pipeline, std::make_shared<ModelManager>(QString()));

    // The usable custom model appears as an enabled radio (it is installed).
    QRadioButton* customRadio = nullptr;
    for (QRadioButton* radio : window.findChildren<QRadioButton*>()) {
        if (radio->text() == QLatin1String("custom-scunet.gguf")) {
            customRadio = radio;
        }
    }
    QVERIFY2(customRadio, "custom model radio missing");
    QVERIFY(customRadio->isEnabled());

    // The unrecognized file must not offer itself as a model; it appears as
    // a disabled (grayed) entry instead.
    for (QRadioButton* radio : window.findChildren<QRadioButton*>()) {
        QVERIFY2(radio->text() != QLatin1String("some-rmbg.gguf"),
                 "unrecognized file must not be selectable");
    }
    QLabel* grayed = nullptr;
    for (QLabel* label : window.findChildren<QLabel*>()) {
        if (label->text() == QLatin1String("some-rmbg.gguf")) {
            grayed = label;
        }
    }
    QVERIFY2(grayed, "unrecognized file should be listed grayed out");
    QVERIFY(!grayed->isEnabled());
    QVERIFY2(!grayed->toolTip().isEmpty(), "grayed rows should explain why via tooltip");
}

void TestMainWindow::defaultExportPathIsAbsoluteAndDerivedFromTheSourceName() {
    // Regression guard for the AppImage bug: a relative default made the
    // export dialog resolve against the process's cwd.
    const QString path = MainWindow::defaultExportPath(QStringLiteral("photos/vacation.png"),
                                                         QStringLiteral(".png"));

    QVERIFY(QFileInfo(path).isAbsolute());
    QVERIFY(path.endsWith(QStringLiteral("vacation_cutout.png")));
}

void TestMainWindow::defaultExportPathFallsBackToOutputWithoutASource() {
    const QString path = MainWindow::defaultExportPath(QString(), QStringLiteral(".png"));

    QVERIFY(QFileInfo(path).isAbsolute());
    QVERIFY(path.endsWith(QStringLiteral("output.png")));
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
