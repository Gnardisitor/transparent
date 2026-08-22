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

// Median-cut color quantization, capped at maxColors. Operates on a flat,
// bounded sample of opaque pixels drawn from every frame combined, so all
// frames of one GIF share a single global color table instead of each
// getting (and paying the encoding cost of) its own local one.
std::vector<Rgb> quantize(std::vector<Rgb> pixels, int maxColors) {
    if (pixels.empty()) {
        return {Rgb{0, 0, 0}};
    }

    // Buckets are contiguous ranges [begin, end) within `pixels`, split
    // repeatedly along whichever bucket+channel currently has the widest
    // value spread until there are maxColors of them (or every bucket is
    // down to a single pixel and can't be split further).
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
        // push_back may reallocate buckets' storage, so this indexes fresh
        // afterward instead of writing through a reference taken before it.
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

// Nearest color within palette[0, searchCount) (the real, non-transparent,
// non-padding entries) by squared Euclidean distance, cached by exact input
// color since real images repeat colors constantly.
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

std::vector<Frame> readFrames(const QString& path) {
    std::vector<Frame> frames;
    QImageReader reader(path);
    if (!reader.canRead()) {
        return frames;
    }

    QImage image = reader.read();
    while (!image.isNull()) {
        Frame frame;
        frame.image = image.convertToFormat(QImage::Format_RGBA8888);
        const int delayMs = reader.nextImageDelay();
        // 0 (or unreported) delay means "as fast as possible"; most viewers
        // substitute a small default instead of a literal 0, so this does
        // too rather than writing a delay GIF players will special-case.
        frame.delayCs = delayMs > 0 ? std::max(1, delayMs / 10) : 10;
        frames.push_back(std::move(frame));
        image = reader.read();
    }
    return frames;
}

bool writeFrames(const QString& path, const std::vector<Frame>& frames) {
    if (frames.empty()) {
        return false;
    }

    const int width = frames.front().image.width();
    const int height = frames.front().image.height();
    if (width <= 0 || height <= 0) {
        return false;
    }

    // One shared palette across every frame, built from a bounded sample of
    // opaque pixels so quantization cost doesn't scale with resolution *
    // frame count.
    constexpr int kMaxSamples = 20000;
    const int perFrameBudget = std::max(1, kMaxSamples / static_cast<int>(frames.size()));
    std::vector<Rgb> samples;
    bool anyTransparent = false;
    for (const Frame& f : frames) {
        const QImage rgba = f.image.convertToFormat(QImage::Format_RGBA8888);
        const int total = rgba.width() * rgba.height();
        const int stride = std::max(1, total / perFrameBudget);
        const uchar* bits = rgba.constBits();
        for (int i = 0; i < total; i += stride) {
            const uchar* px = bits + static_cast<ptrdiff_t>(i) * 4;
            if (px[3] < 128) {
                anyTransparent = true;
                continue;
            }
            samples.push_back({px[0], px[1], px[2]});
        }
    }

    // Reserve one palette slot for transparency (GIF has no soft alpha —
    // BackgroundRemovalStep's smooth mask edge gets thresholded here) so
    // there's somewhere for it to point.
    const int maxColors = anyTransparent ? 255 : 256;
    std::vector<Rgb> palette = quantize(std::move(samples), maxColors);
    const int realColorCount = static_cast<int>(palette.size());
    const int transparentIndex = anyTransparent ? realColorCount : -1;
    if (anyTransparent) {
        palette.push_back({0, 0, 0});
    }

    // giflib's color table size must be a power of two.
    int colorCount = 2;
    while (colorCount < static_cast<int>(palette.size())) {
        colorCount *= 2;
    }
    palette.resize(colorCount, Rgb{0, 0, 0});

    std::vector<GifColorType> gifColors(palette.size());
    for (size_t i = 0; i < palette.size(); ++i) {
        gifColors[i] = {palette[i].r, palette[i].g, palette[i].b};
    }
    ColorMapObject* colorMap = GifMakeMapObject(colorCount, gifColors.data());
    if (!colorMap) {
        return false;
    }

    int errorCode = 0;
    const QByteArray pathBytes = QFile::encodeName(path);
    GifFileType* gif = EGifOpenFileName(pathBytes.constData(), false, &errorCode);
    if (!gif) {
        GifFreeMapObject(colorMap);
        return false;
    }

    // Background index matches the transparent slot (when there is one) so
    // DISPOSE_BACKGROUND below clears to "nothing" between frames instead of
    // a solid palette color.
    const int backgroundIndex = transparentIndex >= 0 ? transparentIndex : 0;
    bool ok = EGifPutScreenDesc(gif, width, height, GifBitSize(colorCount), backgroundIndex,
                                 colorMap) == GIF_OK;

    // NETSCAPE2.0 application extension: the de facto standard way to make
    // an animated GIF loop forever instead of playing once.
    if (ok) {
        static const unsigned char kNetscape[] = {'N', 'E', 'T', 'S', 'C', 'A', 'P',
                                                    'E', '2', '.', '0'};
        static const unsigned char kLoopForever[] = {1, 0, 0};
        ok = EGifPutExtensionLeader(gif, APPLICATION_EXT_FUNC_CODE) == GIF_OK &&
             EGifPutExtensionBlock(gif, sizeof(kNetscape), kNetscape) == GIF_OK &&
             EGifPutExtensionBlock(gif, sizeof(kLoopForever), kLoopForever) == GIF_OK &&
             EGifPutExtensionTrailer(gif) == GIF_OK;
    }

    std::unordered_map<uint32_t, int> colorCache;
    std::vector<GifByteType> indices(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t frameIdx = 0; ok && frameIdx < frames.size(); ++frameIdx) {
        const QImage rgba = frames[frameIdx].image.convertToFormat(QImage::Format_RGBA8888);
        if (rgba.width() != width || rgba.height() != height) {
            ok = false;
            break;
        }

        const uchar* bits = rgba.constBits();
        for (int i = 0; i < width * height; ++i) {
            const uchar* px = bits + static_cast<ptrdiff_t>(i) * 4;
            if (transparentIndex >= 0 && px[3] < 128) {
                indices[static_cast<size_t>(i)] = static_cast<GifByteType>(transparentIndex);
            } else {
                indices[static_cast<size_t>(i)] = static_cast<GifByteType>(nearestPaletteIndex(
                    palette, realColorCount, {px[0], px[1], px[2]}, colorCache));
            }
        }

        GraphicsControlBlock gcb{};
        // Every frame here is a complete, independently-processed image
        // (not a delta against the previous one), so each frame must clear
        // to background first — DISPOSE_DO_NOT would let a transparent
        // pixel reveal whatever the *previous* frame drew underneath it,
        // ghosting stale content through wherever the current frame's
        // subject silhouette doesn't cover.
        gcb.DisposalMode = DISPOSE_BACKGROUND;
        gcb.DelayTime = frames[frameIdx].delayCs;
        gcb.TransparentColor = transparentIndex >= 0 ? transparentIndex : NO_TRANSPARENT_COLOR;

        GifByteType extension[4];
        EGifGCBToExtension(&gcb, extension);
        ok = EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, sizeof(extension), extension) == GIF_OK &&
             EGifPutImageDesc(gif, 0, 0, width, height, false, nullptr) == GIF_OK &&
             EGifPutLine(gif, indices.data(), width * height) == GIF_OK;
    }

    ok = EGifCloseFile(gif, &errorCode) == GIF_OK && ok;
    GifFreeMapObject(colorMap);
    return ok;
}

} // namespace GifIO
