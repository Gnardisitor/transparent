#include "BokehStep.h"

#include <QDebug>

#include <algorithm>
#include <cmath>

namespace {

// Separable box blur, clamped to the image edge. Three passes of a box blur
// is a standard cheap approximation of a Gaussian blur, good enough for a
// mask-only bokeh effect without pulling in a dedicated image-processing
// dependency. radius == 0 is a valid no-op (windowSize == 1, each output
// pixel is just its own input pixel), used at 0% strength.
QImage horizontalBoxBlur(const QImage& rgb, int radius) {
    const int width = rgb.width();
    const int height = rgb.height();
    const int windowSize = 2 * radius + 1;

    QImage out(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        const uchar* srcRow = rgb.constScanLine(y);
        uchar* dstRow = out.scanLine(y);
        for (int c = 0; c < 3; ++c) {
            int sum = 0;
            for (int dx = -radius; dx <= radius; ++dx) {
                const int sx = std::clamp(dx, 0, width - 1);
                sum += srcRow[sx * 3 + c];
            }
            for (int x = 0; x < width; ++x) {
                dstRow[x * 3 + c] = static_cast<uchar>(sum / windowSize);
                if (x + 1 < width) {
                    const int removeX = std::clamp(x - radius, 0, width - 1);
                    const int addX = std::clamp(x + 1 + radius, 0, width - 1);
                    sum += srcRow[addX * 3 + c] - srcRow[removeX * 3 + c];
                }
            }
        }
    }
    return out;
}

QImage verticalBoxBlur(const QImage& rgb, int radius) {
    const int width = rgb.width();
    const int height = rgb.height();
    const int windowSize = 2 * radius + 1;

    QImage out(width, height, QImage::Format_RGB888);
    for (int x = 0; x < width; ++x) {
        for (int c = 0; c < 3; ++c) {
            int sum = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int sy = std::clamp(dy, 0, height - 1);
                sum += rgb.constScanLine(sy)[x * 3 + c];
            }
            for (int y = 0; y < height; ++y) {
                out.scanLine(y)[x * 3 + c] = static_cast<uchar>(sum / windowSize);
                if (y + 1 < height) {
                    const int removeY = std::clamp(y - radius, 0, height - 1);
                    const int addY = std::clamp(y + 1 + radius, 0, height - 1);
                    sum += rgb.constScanLine(addY)[x * 3 + c] - rgb.constScanLine(removeY)[x * 3 + c];
                }
            }
        }
    }
    return out;
}

QImage boxBlur(const QImage& rgb, int radius) {
    QImage result = rgb;
    for (int pass = 0; pass < 3; ++pass) {
        result = verticalBoxBlur(horizontalBoxBlur(result, radius), radius);
    }
    return result;
}

} // namespace

QString BokehStep::settingsKey() {
    return QStringLiteral("bokeh/strengthPercent");
}

BokehStep::BokehStep(std::shared_ptr<SegmentationModel> model, int strengthPercent)
    : model_(std::move(model)), strengthPercent_(std::clamp(strengthPercent, 0, 100)) {}

void BokehStep::setModel(std::shared_ptr<SegmentationModel> model) {
    model_ = std::move(model);
    // A different model means a different mask — the cached one, if any, no
    // longer corresponds to what this model would produce.
    cachedInput_ = QImage();
    cachedMask_ = QImage();
}

void BokehStep::setStrengthPercent(int percent) {
    strengthPercent_ = std::clamp(percent, 0, 100);
}

int BokehStep::radiusForImage(const QImage& image, int strengthPercent) const {
    const int shorterSide = std::min(image.width(), image.height());
    return static_cast<int>(std::lround(std::clamp(strengthPercent, 0, 100) / 100.0 * 0.05 * shorterSide));
}

QImage BokehStep::blend(const QImage& input, const QImage& mask, int radius) const {
    const QImage sharp = input.convertToFormat(QImage::Format_RGB888);
    const QImage blurred = boxBlur(sharp, radius);
    const QImage alphaMask = mask.convertToFormat(QImage::Format_Alpha8);

    const bool preserveAlpha = input.hasAlphaChannel();
    const QImage inputAlpha = preserveAlpha ? input.convertToFormat(QImage::Format_Alpha8) : QImage();

    const int width = input.width();
    const int height = input.height();

    QImage output(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y) {
        const uchar* sharpRow = sharp.constScanLine(y);
        const uchar* blurredRow = blurred.constScanLine(y);
        const uchar* maskRow = alphaMask.constScanLine(y);
        const uchar* alphaRow = preserveAlpha ? inputAlpha.constScanLine(y) : nullptr;
        uchar* dstRow = output.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int weight = maskRow[x];
            for (int c = 0; c < 3; ++c) {
                const int sharpValue = sharpRow[x * 3 + c];
                const int blurredValue = blurredRow[x * 3 + c];
                dstRow[x * 4 + c] = static_cast<uchar>(
                    blurredValue + ((sharpValue - blurredValue) * weight) / 255);
            }
            dstRow[x * 4 + 3] = preserveAlpha ? alphaRow[x] : 255;
        }
    }

    return output;
}

QImage BokehStep::process(const QImage& input) const {
    if (!isReady() || input.isNull()) {
        return input;
    }

    const QImage mask = model_->computeMask(input);
    if (mask.isNull() || mask.size() != input.size()) {
        if (!mask.isNull()) {
            qWarning() << "BokehStep: mask size" << mask.size() << "doesn't match input size"
                       << input.size();
        }
        return input;
    }

    cachedInput_ = input;
    cachedMask_ = mask;

    return blend(input, mask, radiusForImage(input, strengthPercent_));
}

QImage BokehStep::reblendCached(int strengthPercent) const {
    if (cachedMask_.isNull() || cachedInput_.isNull()) {
        return QImage();
    }
    return blend(cachedInput_, cachedMask_, radiusForImage(cachedInput_, strengthPercent));
}
