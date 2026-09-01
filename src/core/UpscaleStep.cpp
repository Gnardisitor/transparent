#include "UpscaleStep.h"

UpscaleStep::UpscaleStep(std::shared_ptr<UpscaleModel> model) : model_(std::move(model)) {}

QImage UpscaleStep::process(const QImage& input, PipelineRun& /*run*/) const {
    if (!isReady() || input.isNull()) {
        return input;
    }

    const QImage upscaled = model_->upscale(input);
    if (upscaled.isNull()) {
        return input;
    }

    if (!input.hasAlphaChannel()) {
        return upscaled;
    }

    const QImage inputAlpha = input.convertToFormat(QImage::Format_Alpha8);
    const QImage scaledAlpha = inputAlpha.scaled(upscaled.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QImage result = upscaled.convertToFormat(QImage::Format_RGBA8888);
    const int width = result.width();
    const int height = result.height();
    for (int y = 0; y < height; ++y) {
        uchar* dstRow = result.scanLine(y);
        const uchar* alphaRow = scaledAlpha.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            dstRow[x * 4 + 3] = alphaRow[x];
        }
    }
    return result;
}
