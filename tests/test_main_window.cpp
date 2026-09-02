#include <QtTest>

#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>

#include "core/ModelCatalog.h"
#include "core/ModelManager.h"
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
    void defaultExportPathIsAbsoluteAndDerivedFromTheSourceName();
    void defaultExportPathFallsBackToOutputWithoutASource();
};

void TestMainWindow::constructionLeavesDefaultStepEnabledDespiteAdvancedPagePopulation() {
    auto pipeline = std::make_shared<Pipeline>();
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Background Removal")));
    pipeline->addStep(std::make_shared<NamedStep>(QStringLiteral("Upscaling")));

    MainWindow window(pipeline);

    // Regression test: building the Advanced-mode step list used to fire
    // QListWidget::itemChanged reentrantly during population: each of
    // QListWidgetItem's own construction/setData/setFlags calls emits it at
    // least once, always reporting the item's not-yet-set default
    // Qt::Unchecked state, before setCheckState() ever runs. Left
    // unblocked, the itemChanged handler wrote that transient Unchecked
    // back into Pipeline, silently disabling Background Removal (the step
    // MainWindow's constructor had just enabled as Simple mode's default),
    // and by the time setCheckState() itself ran, isStepEnabled() already
    // read back that corrupted false and "confirmed" Unchecked instead of
    // overwriting it, so nothing ever restored the correct state. In
    // practice this meant the very first image dropped after launch always
    // passed through unprocessed. See populateAdvancedStepList()'s
    // QSignalBlocker for the fix.
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
