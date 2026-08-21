#include "core/BackgroundRemovalStep.h"
#include "core/Pipeline.h"
#include "core/UpscaleStep.h"
#include "core/VisionCppSegmentationModel.h"
#include "core/VisionCppUpscaleModel.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include <memory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("transparent"));
    QApplication::setOrganizationName(QStringLiteral("transparent"));

    auto pipeline = std::make_shared<Pipeline>();

    auto model = std::make_shared<VisionCppSegmentationModel>(
        QStringLiteral(TRANSPARENT_MODELS_DIR "/BiRefNet-lite-F16.gguf"));
    auto backgroundRemoval = std::make_shared<BackgroundRemovalStep>(model);
    if (backgroundRemoval->isReady()) {
        pipeline->addStep(backgroundRemoval);
    }

    auto upscaleModel = std::make_shared<VisionCppUpscaleModel>(
        QStringLiteral(TRANSPARENT_MODELS_DIR "/ESRGAN-4x-foolhardy_Remacri-F16.gguf"));
    auto upscale = std::make_shared<UpscaleStep>(upscaleModel);
    if (upscale->isReady()) {
        pipeline->addStep(upscale);
    }
    // Which step ends up enabled by default (Background Removal) is decided
    // by MainWindow, which re-derives it from each step's name every time it
    // builds the operation dropdown — not here.

    MainWindow window(pipeline);
    window.resize(900, 700);
    window.show();

    return QApplication::exec();
}
