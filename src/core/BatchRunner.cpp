#include "BatchRunner.h"
#include "GifIO.h"
#include "ImageFormats.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSet>

BatchRunner::BatchRunner(std::shared_ptr<Pipeline> pipeline) : pipeline_(std::move(pipeline)) {}

QStringList BatchRunner::discoverImages(const QString& folderPath) const {
    QDir dir(folderPath);
    dir.setFilter(QDir::Files);
    dir.setSorting(QDir::Name);

    QStringList result;
    for (const QFileInfo& info : dir.entryInfoList()) {
        if (ImageFormats::isSupported(info.filePath())) {
            result << info.absoluteFilePath();
        }
    }
    return result;
}

BatchResult BatchRunner::run(const QString& inputFolder, const QString& outputFolder,
                              const std::function<void(int, int, const QString&)>& onProgress,
                              const std::optional<std::vector<size_t>>& stepOrder) const {
    QDir().mkpath(outputFolder);

    const QStringList images = discoverImages(inputFolder);
    const QDir outputDir(outputFolder);

    BatchResult result;
    // cat.png and cat.jpg both map to cat.png; the second one fails
    // instead of silently overwriting the first.
    QSet<QString> claimedOutputNames;
    for (int i = 0; i < images.size(); ++i) {
        const QFileInfo info(images[i]);
        const bool animated = GifIO::isAnimated(info.filePath());
        const QString outputName =
            info.completeBaseName() + (animated ? QStringLiteral(".gif") : QStringLiteral(".png"));

        bool ok = false;
        if (!claimedOutputNames.contains(outputName)) {
            if (animated) {
                ok = processAnimatedGif(info.filePath(), outputDir.filePath(outputName), stepOrder);
            } else {
                const QImage source(info.filePath());
                ok = !source.isNull();
                if (ok) {
                    QImage processed = source;
                    if (pipeline_) {
                        processed = stepOrder.has_value() ? pipeline_->run(source, *stepOrder)
                                                           : pipeline_->run(source);
                    }
                    ok = processed.save(outputDir.filePath(outputName), "PNG");
                }
            }
            if (ok) {
                claimedOutputNames.insert(outputName);
            }
        }

        if (ok) {
            ++result.succeeded;
        } else {
            result.failedFiles << info.fileName();
        }

        if (onProgress) {
            onProgress(i + 1, images.size(), info.fileName());
        }
    }

    return result;
}

// Frames stream one at a time: decode, run through the pipeline, encode,
// release. Peak memory is a single source frame plus a single processed
// frame, not the whole processed animation (a 4x upscale multiplies every
// frame's footprint by 16). The palette is fixed by the first processed
// frame, so its colors define the whole output.
bool BatchRunner::processAnimatedGif(const QString& inputPath, const QString& outputPath,
                                       const std::optional<std::vector<size_t>>& stepOrder) const {
    GifIO::Writer writer;
    if (!writer.open(outputPath)) {
        return false;
    }

    GifIO::Reader reader(inputPath);
    while (std::optional<GifIO::Frame> frame = reader.next()) {
        QImage processed;
        if (pipeline_) {
            processed = stepOrder.has_value() ? pipeline_->run(frame->image, *stepOrder)
                                               : pipeline_->run(frame->image);
        } else {
            processed = frame->image;
        }
        if (!writer.encode({std::move(processed), frame->delayCs})) {
            return false;
        }
    }
    // finish() fails when no frame was encoded, covering unreadable input.
    return writer.finish();
}
