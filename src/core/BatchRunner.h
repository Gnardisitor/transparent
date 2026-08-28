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
    // result as "<basename>.png" under outputFolder. Animated GIFs are the
    // exception: every frame runs through the pipeline and the result is
    // saved as "<basename>.gif". Load/save failures or duplicate output
    // names (cat.png and cat.jpg) are recorded and don't stop the batch.
    // onProgress, if set, is called per image with (done, total, fileName).
    // stepOrder, if set, is forwarded to Pipeline::run(image, stepOrder).
    BatchResult run(const QString& inputFolder, const QString& outputFolder,
                     const std::function<void(int, int, const QString&)>& onProgress = nullptr,
                     const std::optional<std::vector<size_t>>& stepOrder = std::nullopt) const;

private:
    std::shared_ptr<Pipeline> pipeline_;
};
