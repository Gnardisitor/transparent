#pragma once

#include <QImage>
#include <QString>

#include <vector>

// Frame-level GIF read/write shared by MainWindow and BatchRunner.
// Decoding goes through Qt's GIF plugin; encoding uses giflib (vcpkg, MIT),
// which has no quantizer, so GifIO owns a small median-cut quantizer.
namespace GifIO {

// delayCs is GIF's hundredths-of-a-second unit, kept end-to-end.
struct Frame {
    QImage image;
    int delayCs = 10;
};

// True only for a GIF with more than one frame; single-frame GIFs are
// treated as static images.
bool isAnimated(const QString& path);

// Every frame of the GIF at path, converted to Format_RGBA8888. Empty if
// the file can't be read.
std::vector<Frame> readFrames(const QString& path);

// Writes a looping animated GIF sharing one quantized palette. GIF
// transparency is on/off, so alpha below 128 is thresholded fully
// transparent (a format limitation, not a bug). Returns false if frames is
// empty or the file can't be written.
bool writeFrames(const QString& path, const std::vector<Frame>& frames);

} // namespace GifIO
