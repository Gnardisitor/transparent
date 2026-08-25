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
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QNetworkProxyFactory>
#include <QSettings>

#include <memory>

namespace {

QString bundledModelsDefaultsDir() {
    const QString installedDir =
        QDir(QCoreApplication::applicationDirPath() +
             QStringLiteral("/../share/transparent/models"))
            .absolutePath();
    if (QDir(installedDir).exists()) {
        return installedDir;
    }
    return QStringLiteral(TRANSPARENT_MODELS_DIR);
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("transparent"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/transparent.png")));

    QNetworkProxyFactory::setUseSystemConfiguration(true);

    auto modelManager = std::make_shared<ModelManager>(bundledModelsDefaultsDir());
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

    auto bokeh = std::make_shared<BokehStep>(segmentationModel, bokehStrengthPercent);
    if (bokeh->isReady()) {
        pipeline->addStep(bokeh);
    }

    MainWindow window(pipeline, modelManager);
    window.resize(900, 700);
    window.show();

    return QApplication::exec();
}
