#pragma once

#include "Pipeline.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Outcome of one run(): succeeded count and failed file names.
struct BatchResult {
    int succeeded = 0;
    QStringList failedFiles;
};

// Runs every supported image in a folder through the Pipeline and writes
// each result into an output folder.
class BatchRunner {
public:
    explicit BatchRunner(std::shared_ptr<Pipeline> pipeline);

    // Non-recursive: files directly inside folderPath whose extension is in
    // ImageFormats::supportedExtensions(), sorted by name.
    QStringList discoverImages(const QString& folderPath) const;

    // Runs the pipeline over discoverImages(inputFolder), saving each
    // result as "<basename>.png" under outputFolder; animated GIFs go out
    // as "<basename>.gif", streamed frame by frame. Load/save failures and
    // duplicate output names (cat.png and cat.jpg) are recorded and don't
    // stop the batch. onProgress, if set, is called per image with (done,
    // total, fileName) from the thread running run(); stepOrder, if set,
    // is forwarded to Pipeline::run(image, stepOrder).
    BatchResult run(const QString& inputFolder, const QString& outputFolder,
                     const std::function<void(int, int, const QString&)>& onProgress = nullptr,
                     const std::optional<std::vector<size_t>>& stepOrder = std::nullopt) const;

private:
    // Streams one animated GIF: decode, run through the pipeline, encode,
    // release, next frame.
    bool processAnimatedGif(const QString& inputPath, const QString& outputPath,
                             const std::optional<std::vector<size_t>>& stepOrder) const;

    std::shared_ptr<Pipeline> pipeline_;
};
