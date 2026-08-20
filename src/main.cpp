#include "core/BackgroundRemovalStep.h"
#include "core/Pipeline.h"
#include "core/VisionCppSegmentationModel.h"
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

    MainWindow window(pipeline);
    window.resize(900, 700);
    window.show();

    return QApplication::exec();
}
