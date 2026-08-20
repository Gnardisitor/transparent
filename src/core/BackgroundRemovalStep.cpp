#include "BackgroundRemovalStep.h"

#include <QDebug>

BackgroundRemovalStep::BackgroundRemovalStep(std::shared_ptr<SegmentationModel> model)
    : model_(std::move(model)) {}

QImage BackgroundRemovalStep::process(const QImage& input) const {
    if (!isReady() || input.isNull()) {
        return input;
    }

    const QImage mask = model_->computeMask(input);
    if (mask.isNull() || mask.size() != input.size()) {
        if (!mask.isNull()) {
            qWarning() << "BackgroundRemovalStep: mask size" << mask.size()
                       << "doesn't match input size" << input.size();
        }
        return input;
    }

    const QImage rgbInput = input.convertToFormat(QImage::Format_RGB888);
    const QImage alphaMask = mask.convertToFormat(QImage::Format_Alpha8);

    const int width = input.width();
    const int height = input.height();

    QImage output(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y) {
        const uchar* srcRow = rgbInput.constScanLine(y);
        const uchar* maskRow = alphaMask.constScanLine(y);
        // Format_RGBA8888 is a byte-order format (R,G,B,A in memory,
        // regardless of endianness) — write raw bytes directly rather than
        // going through qRgba(), which packs Format_ARGB32's bit layout
        // (0xAARRGGBB) and would swap red/blue once reinterpreted as bytes
        // on a little-endian machine.
        uchar* dstRow = output.scanLine(y);
        for (int x = 0; x < width; ++x) {
            dstRow[x * 4 + 0] = srcRow[x * 3 + 0]; // R
            dstRow[x * 4 + 1] = srcRow[x * 3 + 1]; // G
            dstRow[x * 4 + 2] = srcRow[x * 3 + 2]; // B
            dstRow[x * 4 + 3] = maskRow[x];        // A
        }
    }

    return output;
}
