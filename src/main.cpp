#include "core/BackgroundRemovalStep.h"
#include "core/BokehStep.h"
#include "core/ModelCatalog.h"
#include "core/ModelManager.h"
#include "core/Pipeline.h"
#include "core/UpscaleStep.h"
#include "core/VisionCppSegmentationModel.h"
#include "core/VisionCppUpscaleModel.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QSettings>

#include <memory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("transparent"));
    QApplication::setOrganizationName(QStringLiteral("transparent"));

    // ModelManager's runtime models directory is QStandardPaths::
    // AppDataLocation, not the build tree — TRANSPARENT_MODELS_DIR is only
    // used here, to give a fresh build a working first run without any
    // network access: ensureDefaultsProvisioned() copies the two
    // CMake-time-downloaded defaults into AppData if they aren't already
    // there. See PLAN.md's Model management section.
    auto modelManager = std::make_shared<ModelManager>(QStringLiteral(TRANSPARENT_MODELS_DIR));
    modelManager->ensureDefaultsProvisioned();

    QSettings settings;
    const QString segmentationFilename =
        settings
            .value(ModelCatalog::settingsKey(ModelCategory::Segmentation),
                   ModelCatalog::defaultFilename(ModelCategory::Segmentation))
            .toString();
    const QString upscaleFilename =
        settings
            .value(ModelCatalog::settingsKey(ModelCategory::Upscale),
                   ModelCatalog::defaultFilename(ModelCategory::Upscale))
            .toString();
    const int bokehStrengthPercent =
        settings.value(BokehStep::settingsKey(), BokehStep::kDefaultStrengthPercent).toInt();

    auto pipeline = std::make_shared<Pipeline>();

    auto segmentationModel = std::make_shared<VisionCppSegmentationModel>(
        modelManager->pathFor(segmentationFilename));
    auto backgroundRemoval = std::make_shared<BackgroundRemovalStep>(segmentationModel);
    if (backgroundRemoval->isReady()) {
        pipeline->addStep(backgroundRemoval);
    }

    auto upscaleModel =
        std::make_shared<VisionCppUpscaleModel>(modelManager->pathFor(upscaleFilename));
    auto upscale = std::make_shared<UpscaleStep>(upscaleModel);
    if (upscale->isReady()) {
        pipeline->addStep(upscale);
    }

    // Reuses the same segmentation model as background removal: the mask it
    // already produces is exactly what a mask-only bokeh effect needs, no
    // separate depth model required for this first cut (see PLAN.md).
    auto bokeh = std::make_shared<BokehStep>(segmentationModel, bokehStrengthPercent);
    if (bokeh->isReady()) {
        pipeline->addStep(bokeh);
    }
    // Which step ends up enabled by default (Background Removal) is decided
    // by MainWindow, which re-derives it from each step's name every time it
    // builds the operation dropdown — not here.

    MainWindow window(pipeline, modelManager);
    window.resize(900, 700);
    window.show();

    return QApplication::exec();
}
