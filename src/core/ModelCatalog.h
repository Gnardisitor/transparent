#pragma once

#include <QString>

#include <optional>
#include <vector>

// Segmentation backs both BackgroundRemovalStep and BokehStep (they share
// one SegmentationModel); Upscale backs UpscaleStep; Denoise backs DenoiseStep.
enum class ModelCategory { Segmentation, Upscale, Denoise };

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

// Map from a GGUF `general.architecture` value to the app category whose
// model seam can load it (birefnet -> Segmentation, scunet -> Denoise,
// esrgan -> Upscale). nullopt for unknown architectures and for arches
// vision.cpp recognizes but this app has no seam for (migan, depthanything,
// mobile-sam) — the model-management scan uses this to classify files.
std::optional<ModelCategory> categoryForArchitecture(const QString& architecture);

// True when vision.cpp knows the architecture at all, even without a seam.
bool isRecognizedArchitecture(const QString& architecture);

// The architecture that loads in `category` (the inverse of the mapping
// above); empty string if none — used in user-facing error messages.
QString architectureForCategory(ModelCategory category);

} // namespace ModelCatalog
