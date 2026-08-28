#pragma once

#include <QString>

#include <vector>

// Segmentation backs both BackgroundRemovalStep and BokehStep (they share
// one SegmentationModel); Upscale backs UpscaleStep.
enum class ModelCategory { Segmentation, Upscale };

// One curated, downloadable model checkpoint, fixed at compile time.
// `filename` doubles as the on-disk identity: a model counts as installed
// purely by matching this name in the models directory.
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

// QSettings key each category's active-model filename is persisted under,
// shared by main.cpp and MainWindow.
QString settingsKey(ModelCategory category);

} // namespace ModelCatalog
