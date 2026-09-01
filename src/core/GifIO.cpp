#include "GifIO.h"

#include <gif_lib.h>

#include <QFile>
#include <QImageReader>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace {

struct Rgb {
    uint8_t r, g, b;
};

// Median-cut quantization capped at maxColors, over a bounded sample of
// opaque pixels from all frames combined so they share one color table.
std::vector<Rgb> quantize(std::vector<Rgb> pixels, int maxColors) {
    if (pixels.empty()) {
        return {Rgb{0, 0, 0}};
    }

    // Split the bucket with the widest channel spread until there are
    // maxColors buckets.
    struct Bucket {
        int begin;
        int end;
    };
    std::vector<Bucket> buckets{{0, static_cast<int>(pixels.size())}};

    auto channelValue = [](const Rgb& p, int channel) {
        return channel == 0 ? p.r : channel == 1 ? p.g : p.b;
    };
    auto bucketRange = [&](const Bucket& b, int channel) {
        int lo = 255, hi = 0;
        for (int i = b.begin; i < b.end; ++i) {
            const int v = channelValue(pixels[i], channel);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        return hi - lo;
    };

    while (static_cast<int>(buckets.size()) < maxColors) {
        int splitIdx = -1, bestRange = -1, bestChannel = 0;
        for (int i = 0; i < static_cast<int>(buckets.size()); ++i) {
            if (buckets[i].end - buckets[i].begin <= 1) {
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                const int range = bucketRange(buckets[i], c);
                if (range > bestRange) {
                    bestRange = range;
                    splitIdx = i;
                    bestChannel = c;
                }
            }
        }
        if (splitIdx < 0) {
            break;
        }

        const Bucket range = buckets[splitIdx];
        std::sort(pixels.begin() + range.begin, pixels.begin() + range.end,
                  [&](const Rgb& a, const Rgb& z) {
                      return channelValue(a, bestChannel) < channelValue(z, bestChannel);
                  });
        const int mid = range.begin + (range.end - range.begin) / 2;
        // push_back may reallocate, so index buckets fresh afterward.
        buckets.push_back({mid, range.end});
        buckets[splitIdx].end = mid;
    }

    std::vector<Rgb> palette;
    palette.reserve(buckets.size());
    for (const Bucket& b : buckets) {
        long sumR = 0, sumG = 0, sumB = 0;
        const int count = b.end - b.begin;
        for (int i = b.begin; i < b.end; ++i) {
            sumR += pixels[i].r;
            sumG += pixels[i].g;
            sumB += pixels[i].b;
        }
        palette.push_back({static_cast<uint8_t>(sumR / count), static_cast<uint8_t>(sumG / count),
                            static_cast<uint8_t>(sumB / count)});
    }
    return palette;
}

// Nearest palette color by squared distance, cached by input color.
int nearestPaletteIndex(const std::vector<Rgb>& palette, int searchCount, Rgb color,
                         std::unordered_map<uint32_t, int>& cache) {
    const uint32_t key =
        (static_cast<uint32_t>(color.r) << 16) | (static_cast<uint32_t>(color.g) << 8) | color.b;
    const auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }

    int best = 0;
    int bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < searchCount; ++i) {
        const int dr = static_cast<int>(color.r) - palette[i].r;
        const int dg = static_cast<int>(color.g) - palette[i].g;
        const int db = static_cast<int>(color.b) - palette[i].b;
        const int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    cache.emplace(key, best);
    return best;
}

} // namespace

namespace GifIO {

bool isAnimated(const QString& path) {
    QImageReader reader(path);
    if (!reader.canRead() || QString::compare(reader.format(), "gif", Qt::CaseInsensitive) != 0) {
        return false;
    }
    return reader.imageCount() > 1;
}

Reader::Reader(const QString& path) : reader_(path) {}

std::optional<Frame> Reader::next() {
    const QImage image = reader_.read();
    if (image.isNull()) {
        return std::nullopt;
    }
    Frame frame;
    frame.image = image.convertToFormat(QImage::Format_RGBA8888);
    const int delayMs = reader_.nextImageDelay();
    // 0 delay means "as fast as possible"; write a small default instead
    // of a delay GIF players special-case.
    frame.delayCs = delayMs > 0 ? std::max(1, delayMs / 10) : 10;
    return frame;
}

std::vector<Frame> readFrames(const QString& path) {
    std::vector<Frame> frames;
    Reader reader(path);
    while (std::optional<Frame> frame = reader.next()) {
        frames.push_back(std::move(*frame));
    }
    return frames;
}

// Writer internals: giflib state plus the fixed shared palette.
struct WriterImpl {
    QString path;

    ColorMapObject* colorMap = nullptr;
    GifFileType* gif = nullptr;
    int width = 0;
    int height = 0;
    int realColorCount = 0;
    int transparentIndex = -1;
    bool ok = false;
    bool wroteAny = false;
    bool finished = false;
    std::vector<Rgb> palette;
    std::unordered_map<uint32_t, int> colorCache;
    std::vector<GifByteType> indices;

    int errorCode = 0;

    bool startFile();
};

// Builds the shared palette from opaque-pixel samples: a transparency slot
// is always reserved (pipeline steps may add alpha the source didn't have),
// transparent pixels never enter the samples, and giflib's color table
// size is rounded up to a power of two.
void buildPalette(WriterImpl& impl, std::vector<Rgb> samples) {
    impl.palette = quantize(std::move(samples), 255); // slot 255 = transparency
    impl.realColorCount = static_cast<int>(impl.palette.size());
    impl.transparentIndex = impl.realColorCount;
    impl.palette.push_back({0, 0, 0});

    int colorCount = 2;
    while (colorCount < static_cast<int>(impl.palette.size())) {
        colorCount *= 2;
    }
    impl.palette.resize(colorCount, Rgb{0, 0, 0});

    std::vector<GifColorType> gifColors(impl.palette.size());
    for (size_t i = 0; i < impl.palette.size(); ++i) {
        gifColors[i] = {impl.palette[i].r, impl.palette[i].g, impl.palette[i].b};
    }
    impl.colorMap = GifMakeMapObject(colorCount, gifColors.data());
}

// Deep sample of one frame's opaque pixels. Used for the first encoded
// frame when no palette source was given at construction.
void buildPaletteFromFrame(WriterImpl& impl, const QImage& rgbaFrame) {
    constexpr int kMaxSamples = 20000;
    const int total = rgbaFrame.width() * rgbaFrame.height();
    std::vector<Rgb> samples;
    if (total > 0) {
        const int stride = std::max(1, total / kMaxSamples);
        const uchar* bits = rgbaFrame.constBits();
        for (int i = 0; i < total; i += stride) {
            const uchar* px = bits + static_cast<ptrdiff_t>(i) * 4;
            if (px[3] < 128) {
                continue;
            }
            samples.push_back({px[0], px[1], px[2]});
        }
    }
    buildPalette(impl, std::move(samples));
}

// Same sampling, spread over every frame of a palette source.
void buildPaletteFromFrames(WriterImpl& impl, const std::vector<Frame>& frames) {
    constexpr int kMaxSamples = 20000;
    const int perFrameBudget = std::max(1, kMaxSamples / static_cast<int>(frames.size()));
    std::vector<Rgb> samples;
    for (const Frame& f : frames) {
        const QImage rgba = f.image.convertToFormat(QImage::Format_RGBA8888);
        const int total = rgba.width() * rgba.height();
        const int stride = std::max(1, total / perFrameBudget);
        const uchar* bits = rgba.constBits();
        for (int i = 0; i < total; i += stride) {
            const uchar* px = bits + static_cast<ptrdiff_t>(i) * 4;
            if (px[3] < 128) {
                continue;
            }
            samples.push_back({px[0], px[1], px[2]});
        }
    }
    buildPalette(impl, std::move(samples));
}

// Writes screen descriptor, shared palette and loop extension. Palette and
// frame size must be fixed before this runs.
bool WriterImpl::startFile() {
    const QByteArray pathBytes = QFile::encodeName(path);
    gif = EGifOpenFileName(pathBytes.constData(), false, &errorCode);
    if (!gif || !colorMap) {
        return false;
    }

    // Background index matches the transparent slot so DISPOSE_BACKGROUND
    // clears to nothing between frames.
    bool ok = EGifPutScreenDesc(gif, width, height,
                                 GifBitSize(static_cast<int>(palette.size())),
                                 transparentIndex, colorMap) == GIF_OK;

    // NETSCAPE2.0 extension: makes an animated GIF loop forever.
    if (ok) {
        static const unsigned char kNetscape[] = {'N', 'E', 'T', 'S', 'C', 'A',
                                                   'P', 'E', '2', '.', '0'};
        static const unsigned char kLoopForever[] = {1, 0, 0};
        ok = EGifPutExtensionLeader(gif, APPLICATION_EXT_FUNC_CODE) == GIF_OK &&
             EGifPutExtensionBlock(gif, sizeof(kNetscape), kNetscape) == GIF_OK &&
             EGifPutExtensionBlock(gif, sizeof(kLoopForever), kLoopForever) == GIF_OK &&
             EGifPutExtensionTrailer(gif) == GIF_OK;
    }
    return ok;
}

Writer::Writer(const std::vector<Frame>& paletteSourceFrames) : impl_(std::make_unique<WriterImpl>()) {
    if (!paletteSourceFrames.empty()) {
        buildPaletteFromFrames(*impl_, paletteSourceFrames);
        impl_->width = paletteSourceFrames.front().image.width();
        impl_->height = paletteSourceFrames.front().image.height();
        impl_->indices.resize(static_cast<size_t>(impl_->width) * impl_->height);
    }
}

Writer::~Writer() {
    if (!impl_->finished) {
        finish();
    }
}

bool Writer::open(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    impl_->path = path;
    return true;
}

bool Writer::encode(const Frame& frame) {
    if (!impl_->ok && impl_->gif) {
        return false; // a previous encode failed; the file is incomplete
    }

    const QImage rgba = frame.image.convertToFormat(QImage::Format_RGBA8888);
    if (!impl_->gif) {
        // First encode: fix the frame size and palette (when not given at
        // construction), then write the header.
        if (rgba.width() <= 0 || rgba.height() <= 0) {
            return false;
        }
        impl_->width = rgba.width();
        impl_->height = rgba.height();
        if (!impl_->colorMap) {
            buildPaletteFromFrame(*impl_, rgba);
            impl_->indices.resize(static_cast<size_t>(impl_->width) * impl_->height);
        }
        if (!impl_->startFile()) {
            impl_->ok = false;
            return false;
        }
        impl_->ok = true;
    }

    if (rgba.width() != impl_->width || rgba.height() != impl_->height) {
        impl_->ok = false;
        return false;
    }

    const int width = impl_->width;
    const int height = impl_->height;
    const uchar* bits = rgba.constBits();
    for (int i = 0; i < width * height; ++i) {
        const uchar* px = bits + static_cast<ptrdiff_t>(i) * 4;
        if (px[3] < 128) {
            impl_->indices[static_cast<size_t>(i)] =
                static_cast<GifByteType>(impl_->transparentIndex);
        } else {
            impl_->indices[static_cast<size_t>(i)] = static_cast<GifByteType>(
                nearestPaletteIndex(impl_->palette, impl_->realColorCount, {px[0], px[1], px[2]},
                                     impl_->colorCache));
        }
    }

    GraphicsControlBlock gcb{};
    // Each frame is a complete image, not a delta, so it must clear to
    // background first; DISPOSE_DO_NOT would ghost previous frames
    // through transparent pixels.
    gcb.DisposalMode = DISPOSE_BACKGROUND;
    gcb.DelayTime = frame.delayCs;
    gcb.TransparentColor = impl_->transparentIndex;

    GifByteType extension[4];
    EGifGCBToExtension(&gcb, extension);
    impl_->ok = EGifPutExtension(impl_->gif, GRAPHICS_EXT_FUNC_CODE, sizeof(extension),
                                   extension) == GIF_OK &&
                 EGifPutImageDesc(impl_->gif, 0, 0, width, height, false, nullptr) == GIF_OK &&
                 EGifPutLine(impl_->gif, impl_->indices.data(), width * height) == GIF_OK;
    if (impl_->ok) {
        impl_->wroteAny = true;
    }
    return impl_->ok;
}

bool Writer::finish() {
    if (impl_->finished) {
        return impl_->ok && impl_->wroteAny;
    }
    impl_->finished = true;
    if (impl_->gif) {
        impl_->ok = EGifCloseFile(impl_->gif, &impl_->errorCode) == GIF_OK && impl_->ok;
        impl_->gif = nullptr;
    }
    if (impl_->colorMap) {
        GifFreeMapObject(impl_->colorMap);
        impl_->colorMap = nullptr;
    }
    return impl_->ok && impl_->wroteAny;
}

bool writeFrames(const QString& path, const std::vector<Frame>& frames) {
    if (frames.empty()) {
        return false;
    }

    Writer writer(frames); // all frames are in memory: sample the palette from all of them
    if (!writer.open(path)) {
        return false;
    }
    for (const Frame& frame : frames) {
        if (!writer.encode(frame)) {
            return false;
        }
    }
    return writer.finish();
}

} // namespace GifIO
