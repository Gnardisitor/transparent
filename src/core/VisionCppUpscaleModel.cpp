#include "VisionCppUpscaleModel.h"

#include <QDebug>

#include <cstring>
#include <exception>

VisionCppUpscaleModel::VisionCppUpscaleModel(QString modelPath) : modelPath_(std::move(modelPath)) {
    try {
        backend_.emplace(visp::backend_init());
        model_ = visp::esrgan_load_model(modelPath_.toLocal8Bit().constData(), *backend_);
        ready_ = true;
    } catch (const std::exception& e) {
        qWarning() << "VisionCppUpscaleModel: failed to initialize (model" << modelPath_
                    << "):" << e.what();
    }
}

QImage VisionCppUpscaleModel::upscale(const QImage& input) const {
    if (!ready_ || input.isNull()) {
        return QImage();
    }

    const QImage rgbInput = input.convertToFormat(QImage::Format_RGB888);

    visp::image_view view;
    view.extent = {rgbInput.width(), rgbInput.height()};
    view.stride = static_cast<int>(rgbInput.bytesPerLine());
    view.format = visp::image_format::rgb_u8;
    view.data = rgbInput.constBits();

    try {
        const visp::image_data result = visp::esrgan_compute(model_, view);

        QImage output(result.extent[0], result.extent[1], QImage::Format_RGBA8888);
        const uint8_t* src = result.data.get();
        for (int y = 0; y < output.height(); ++y) {
            std::memcpy(output.scanLine(y), src + static_cast<size_t>(y) * output.width() * 4,
                        static_cast<size_t>(output.width()) * 4);
        }
        return output;
    } catch (const std::exception& e) {
        qWarning() << "VisionCppUpscaleModel: inference failed:" << e.what();
        return QImage();
    }
}
