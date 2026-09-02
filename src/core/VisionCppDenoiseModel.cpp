#include "VisionCppDenoiseModel.h"

#include <QDebug>

#include <cstring>
#include <exception>
#include <vector>

VisionCppDenoiseModel::VisionCppDenoiseModel(QString modelPath) : modelPath_(std::move(modelPath)) {
    try {
        backend_.emplace(visp::backend_init());
        model_ = visp::scunet_load_model(modelPath_.toLocal8Bit().constData(), *backend_);
        ready_ = true;
    } catch (const std::exception& e) {
        qWarning() << "VisionCppDenoiseModel: failed to initialize (model" << modelPath_
                   << "):" << e.what();
    }
}

QImage VisionCppDenoiseModel::denoise(const QImage& input) const {
    if (!ready_ || input.isNull()) {
        return QImage();
    }

    const QImage rgbInput = input.convertToFormat(QImage::Format_RGB888);
    const int width = rgbInput.width();
    const int height = rgbInput.height();

    // Same Qt-scanline-padding stride bug as
    // VisionCppSegmentationModel::computeMask(); repack tightly.
    std::vector<uint8_t> packed(static_cast<size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        std::memcpy(packed.data() + static_cast<size_t>(y) * width * 3, rgbInput.constScanLine(y),
                    static_cast<size_t>(width) * 3);
    }

    visp::image_view view;
    view.extent = {width, height};
    view.stride = width * 3;
    view.format = visp::image_format::rgb_u8;
    view.data = packed.data();

    try {
        const visp::image_data result = visp::scunet_compute(model_, view);

        QImage output(result.extent[0], result.extent[1], QImage::Format_RGBA8888);
        const uint8_t* src = result.data.get();
        for (int y = 0; y < output.height(); ++y) {
            std::memcpy(output.scanLine(y), src + static_cast<size_t>(y) * output.width() * 4,
                        static_cast<size_t>(output.width()) * 4);
        }
        return output;
    } catch (const std::exception& e) {
        qWarning() << "VisionCppDenoiseModel: inference failed:" << e.what();
        return QImage();
    }
}
