#include "VisionCppSegmentationModel.h"

#include <QDebug>

#include <cstring>
#include <exception>
#include <vector>

VisionCppSegmentationModel::VisionCppSegmentationModel(QString modelPath)
    : modelPath_(std::move(modelPath)) {
    try {
        backend_.emplace(visp::backend_init());
        model_ = visp::birefnet_load_model(modelPath_.toLocal8Bit().constData(), *backend_);
        ready_ = true;
    } catch (const std::exception& e) {
        qWarning() << "VisionCppSegmentationModel: failed to initialize (model" << modelPath_
                    << "):" << e.what();
    }
}

QImage VisionCppSegmentationModel::computeMask(const QImage& input) const {
    if (!ready_ || input.isNull()) {
        return QImage();
    }

    const QImage rgbInput = input.convertToFormat(QImage::Format_RGB888);
    const int width = rgbInput.width();
    const int height = rgbInput.height();

    // Qt pads Format_RGB888 scanlines to 4 bytes, which is only a whole
    // number of 3-byte pixels when width % 4 == 0. vision.cpp's stride
    // handling truncates then and corrupts row addressing; repack tightly
    // so the stride is always width * 3.
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
        const visp::image_data maskData = visp::birefnet_compute(model_, view);

        QImage mask(rgbInput.width(), rgbInput.height(), QImage::Format_Alpha8);
        const uint8_t* src = maskData.data.get();
        for (int y = 0; y < rgbInput.height(); ++y) {
            std::memcpy(mask.scanLine(y), src + static_cast<size_t>(y) * rgbInput.width(),
                        static_cast<size_t>(rgbInput.width()));
        }
        return mask;
    } catch (const std::exception& e) {
        qWarning() << "VisionCppSegmentationModel: inference failed:" << e.what();
        return QImage();
    }
}
