#pragma once

#include <QImage>
#include <QString>

#include <vector>

// Loops the same "read frames, process each one, write frames back out"
// shape BatchRunner already applies to a folder of files, but over a single
// animated GIF's frames instead (PLAN.md, Deferred #4 "Video / GIF
// support"). MainWindow and BatchRunner both call into this rather than
// duplicating frame extraction/encoding. Decoding goes through Qt's own GIF
// plugin (already handles disposal methods and per-frame delays correctly);
// encoding uses giflib (vcpkg, MIT), which only reads/writes the container
// format and has no quantizer of its own, so this also owns a small
// median-cut quantizer for building the output palette.
namespace GifIO {

// One decoded/encoded frame. delayCs is GIF's own delay unit — hundredths
// of a second — kept in that unit end-to-end so no unit conversion happens
// between reading and writing.
struct Frame {
    QImage image;
    int delayCs = 10;
};

// True only for a GIF with more than one frame. False (not an error) for a
// single-frame GIF, which the rest of the app already treats as a plain
// static image via QImage.
bool isAnimated(const QString& path);

// Every frame of the GIF at path, already converted to Format_RGBA8888 so
// callers can feed them straight into a Pipeline. Empty if the file can't
// be read.
std::vector<Frame> readFrames(const QString& path);

// Writes frames out as a looping animated GIF, sharing one quantized
// (at most 256-color) palette across every frame. GIF's transparency is a
// single on/off bit rather than BackgroundRemovalStep's smooth alpha
// channel, so any pixel with alpha below 128 is thresholded to fully
// transparent and everything at or above it is fully opaque — an inherent
// GIF format limitation, not a bug in this writer. Returns false if frames
// is empty or the file can't be written.
bool writeFrames(const QString& path, const std::vector<Frame>& frames);

} // namespace GifIO
