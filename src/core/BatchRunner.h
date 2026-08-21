#pragma once

#include "Pipeline.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Outcome of one run(): how many images made it through, and the file names
// (not full paths) of any that didn't, so the caller can report both without
// re-deriving them.
struct BatchResult {
    int succeeded = 0;
    QStringList failedFiles;
};

// "Drag a folder instead of a file" (PLAN.md, Deferred #1) is this class:
// find the supported images directly inside a folder, run the existing
// Pipeline over each one exactly as MainWindow does for a single image, and
// write each result as a PNG into an output folder. No new per-image logic;
// this only adds the folder-of-files loop and bookkeeping around
// Pipeline::run.
class BatchRunner {
public:
    explicit BatchRunner(std::shared_ptr<Pipeline> pipeline);

    // Non-recursive: files directly inside folderPath whose extension is in
    // ImageFormats::supportedExtensions(), sorted by name.
    QStringList discoverImages(const QString& folderPath) const;

    // Runs discoverImages(inputFolder) through the pipeline, saving each
    // result as "<basename>.png" under outputFolder (created if missing). A
    // source image that fails to load, a result that fails to save, or a
    // source image whose output name was already claimed by an earlier one
    // in this batch (e.g. cat.png and cat.jpg both wanting cat.png) counts
    // as a failure and does not stop the rest of the batch. onProgress, if
    // set, is called once per image after it's handled, with
    // (imagesDoneSoFar, totalImages, sourceFileName). stepOrder, if set, is
    // forwarded to Pipeline::run(image, stepOrder) instead of the default
    // fixed-order run(image) — lets an Advanced-mode UI's step
    // selection/ordering apply to folder batches too.
    BatchResult run(const QString& inputFolder, const QString& outputFolder,
                     const std::function<void(int, int, const QString&)>& onProgress = nullptr,
                     const std::optional<std::vector<size_t>>& stepOrder = std::nullopt) const;

private:
    std::shared_ptr<Pipeline> pipeline_;
};
