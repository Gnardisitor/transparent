#pragma once

#include <QString>

#include <vector>

// Which pipeline step a model belongs to. Segmentation backs both
// BackgroundRemovalStep and BokehStep (they share one SegmentationModel);
// Upscale backs UpscaleStep.
enum class ModelCategory { Segmentation, Upscale };

// One curated, downloadable model checkpoint. Every field here is fixed at
// compile time (name, license, size, URL, checksum) — this is the "curated
// registry, not free-form file browsing" design from PLAN.md's Model
// management section. `filename` doubles as the on-disk identity: whether a
// model is installed is decided purely by matching this against what's
// actually present in ModelManager's models directory, so a manually placed
// file is picked up the same way a downloaded one is.
struct ModelInfo {
    ModelCategory category;
    QString displayName;
    QString filename;
    QString license;
    qint64 approxSizeBytes;
    QString url;
    // Lowercase hex SHA-256, verified after download (see ModelManager).
    QString sha256;
};

namespace ModelCatalog {

// Every curated model, in declaration order.
const std::vector<ModelInfo>& allModels();

// Subset of allModels() belonging to one category, in declaration order.
std::vector<ModelInfo> modelsForCategory(ModelCategory category);

// nullptr if no curated entry has this filename.
const ModelInfo* findByFilename(const QString& filename);

// The filename main.cpp falls back to when nothing is persisted in
// QSettings yet, or when a persisted choice no longer matches a known model.
QString defaultFilename(ModelCategory category);

// QSettings key each category's active-model filename is persisted under.
// One place for this so main.cpp (reading it at startup) and MainWindow
// (writing it after a live swap) can't drift apart.
QString settingsKey(ModelCategory category);

} // namespace ModelCatalog
