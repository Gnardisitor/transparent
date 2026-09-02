#include "DenoiseStep.h"

DenoiseStep::DenoiseStep(std::shared_ptr<DenoiseModel> model) : model_(std::move(model)) {}

QImage DenoiseStep::process(const QImage& input, PipelineRun& /*run*/) const {
    if (!isReady() || input.isNull()) {
        return input;
    }

    // The model only sees RGB; alpha never reaches inference.
    const QImage denoised = model_->denoise(input);
    if (denoised.isNull()) {
        return input;
    }

    if (!input.hasAlphaChannel()) {
        return denoised;
    }

    // Same resolution in and out, so the alpha channel can be copied verbatim.
    QImage result = denoised.convertToFormat(QImage::Format_RGBA8888);
    const QImage inputAlpha = input.convertToFormat(QImage::Format_Alpha8);
    for (int y = 0; y < result.height(); ++y) {
        const uchar* alphaRow = inputAlpha.constScanLine(y);
        uchar* dstRow = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            dstRow[x * 4 + 3] = alphaRow[x];
        }
    }
    return result;
}
