#pragma once

#include <QImage>
#include <QImageReader>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

// Frame-level GIF read/write shared by MainWindow and BatchRunner.
// Decoding goes through Qt's GIF plugin; encoding uses giflib (vcpkg, MIT),
// which has no quantizer, so GifIO owns a small median-cut quantizer.
namespace GifIO {

struct WriterImpl;

// delayCs is GIF's hundredths-of-a-second unit, kept end-to-end.
struct Frame {
    QImage image;
    int delayCs = 10;
};

// True only for a GIF with more than one frame; single-frame GIFs are
// treated as static images.
bool isAnimated(const QString& path);

// Incremental GIF reader, the input-side mirror of Writer: one frame per
// next() call instead of materializing the whole animation.
class Reader {
public:
    explicit Reader(const QString& path);

    // Next frame as Format_RGBA8888, with delayCs normalized the way
    // readFrames() does it (0 means "as fast as possible" and is replaced
    // by a small default). nullopt at the end of the animation or when the
    // file can't be read.
    std::optional<Frame> next();

private:
    QImageReader reader_;
};

// Every frame of the GIF at path, converted to Format_RGBA8888. Empty if
// the file can't be read. Convenience over Reader for callers that want
// the whole animation; use Reader to hold one frame at a time.
std::vector<Frame> readFrames(const QString& path);

// Streaming animated-GIF encoder. Frames are encoded and released one at a
// time, so peak memory stays at one frame instead of the whole animation.
// The shared palette must be committed before the first frame is written:
// it is fixed from `paletteSourceFrames` when those are given, or from the
// first encoded frame otherwise. A transparency slot is always reserved
// (pipeline steps may introduce alpha the source didn't have); GIF
// transparency is on/off, so alpha below 128 becomes fully transparent.
class Writer {
public:
    // Without a palette source the palette comes from the first encoded
    // frame; with one, from those frames (for callers that hold them all
    // anyway, like writeFrames).
    explicit Writer(const std::vector<Frame>& paletteSourceFrames = {});
    ~Writer();

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    // Schedules the file; the header is written with the first encode().
    bool open(const QString& path);

    // Appends one processed frame. The first encode fixes the frame size
    // (and the palette, without a source); later frames must match it.
    bool encode(const Frame& frame);

    // Closes the file; the destructor calls it too. False if nothing was
    // encoded or a previous encode failed.
    bool finish();

private:
    std::unique_ptr<WriterImpl> impl_;
};

// Writes a looping animated GIF sharing one quantized palette, built from
// the frames themselves. Convenience over Writer for callers that already
// hold every frame; use Writer directly to keep only one frame in memory.
// Returns false if frames is empty or the file can't be written.
bool writeFrames(const QString& path, const std::vector<Frame>& frames);

} // namespace GifIO
