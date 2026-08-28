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
                std::vector<GifIO::Frame> frames = GifIO::readFrames(info.filePath());
                ok = !frames.empty();
                if (ok && pipeline_) {
                    for (GifIO::Frame& frame : frames) {
                        frame.image = stepOrder.has_value() ? pipeline_->run(frame.image, *stepOrder)
                                                              : pipeline_->run(frame.image);
                    }
                }
                if (ok) {
                    ok = GifIO::writeFrames(outputDir.filePath(outputName), frames);
                }
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
