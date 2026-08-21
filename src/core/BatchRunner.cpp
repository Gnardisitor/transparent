#include "BatchRunner.h"
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
                              const std::function<void(int, int, const QString&)>& onProgress) const {
    QDir().mkpath(outputFolder);

    const QStringList images = discoverImages(inputFolder);
    const QDir outputDir(outputFolder);

    BatchResult result;
    // Two source files that only differ by extension (cat.png and cat.jpg)
    // both map to the same "<basename>.png" output: track claimed names so
    // the second one fails instead of silently overwriting the first.
    QSet<QString> claimedOutputNames;
    for (int i = 0; i < images.size(); ++i) {
        const QFileInfo info(images[i]);
        const QString outputName = info.completeBaseName() + QStringLiteral(".png");

        bool ok = false;
        if (!claimedOutputNames.contains(outputName)) {
            const QImage source(info.filePath());
            ok = !source.isNull();
            if (ok) {
                const QImage processed = pipeline_ ? pipeline_->run(source) : source;
                ok = processed.save(outputDir.filePath(outputName), "PNG");
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
