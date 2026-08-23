#include "ModelCatalog.h"

#include <algorithm>

namespace {

// Sizes and checksums confirmed against huggingface.co/Acly/BiRefNet-GGUF
// and huggingface.co/Acly/Real-ESRGAN-GGUF (their git-lfs blob metadata, not
// downloaded and re-hashed locally). The two defaults' checksums also match
// models/CMakeLists.txt's EXPECTED_HASH values byte for byte.
//
// RealESRGAN-x4plus_anime-6B is deliberately absent: it's a "plus"/
// pixel-shuffle variant that vision.cpp's esrgan_load_model doesn't support
// (see UpscaleStep's Deferred entry in PLAN.md), so it's not a real option
// even though it's published in the same repo.
const std::vector<ModelInfo>& catalog() {
    static const std::vector<ModelInfo> models = {
        {ModelCategory::Segmentation, QStringLiteral("BiRefNet-lite"),
         QStringLiteral("BiRefNet-lite-F16.gguf"), QStringLiteral("MIT"), 88'647'936,
         QStringLiteral(
             "https://huggingface.co/Acly/BiRefNet-GGUF/resolve/main/BiRefNet-lite-F16.gguf"),
         QStringLiteral("7b5397a2c98d66677f8f74317774bbeac49dbb321b8a3dc744af913db71d4fa5")},
        {ModelCategory::Segmentation, QStringLiteral("BiRefNet-dynamic"),
         QStringLiteral("BiRefNet-dynamic-F16.gguf"), QStringLiteral("MIT"), 440'372'864,
         QStringLiteral("https://huggingface.co/Acly/BiRefNet-GGUF/resolve/main/"
                         "BiRefNet-dynamic-F16.gguf"),
         QStringLiteral("c7add90033d5d85fc88e87958484811e41a6e4a30aa6f1e28950b3d81fd3dcf3")},
        {ModelCategory::Segmentation, QStringLiteral("BiRefNet (full)"),
         QStringLiteral("BiRefNet-F16.gguf"), QStringLiteral("MIT"), 440'372'864,
         QStringLiteral("https://huggingface.co/Acly/BiRefNet-GGUF/resolve/main/BiRefNet-F16.gguf"),
         QStringLiteral("5d5fd824c8fb2c1a65fc4345458b2e78777d949418385ea7bba5a9f104364d77")},
        {ModelCategory::Upscale, QStringLiteral("foolhardy_Remacri"),
         QStringLiteral("ESRGAN-4x-foolhardy_Remacri-F16.gguf"), QStringLiteral("BSD-3-Clause"),
         33'451'392,
         QStringLiteral("https://huggingface.co/Acly/Real-ESRGAN-GGUF/resolve/main/"
                         "ESRGAN-4x-foolhardy_Remacri-F16.gguf"),
         QStringLiteral("843aa7c4bcf5919b7f5b72eef8d8cfd9df9949c1837002e3f6d9bf07c1b3af5a")},
        {ModelCategory::Upscale, QStringLiteral("NMKD-Superscale-SP"),
         QStringLiteral("ESRGAN-4x-NMKD-Superscale-SP_178000_G-F16.gguf"),
         QStringLiteral("BSD-3-Clause"), 33'451'424,
         QStringLiteral("https://huggingface.co/Acly/Real-ESRGAN-GGUF/resolve/main/"
                         "ESRGAN-4x-NMKD-Superscale-SP_178000_G-F16.gguf"),
         QStringLiteral("cc8c767fc88109b3b25b3580c159fe7e81cec7a52ccb5158ae78d5709f064ffb")},
    };
    return models;
}

} // namespace

namespace ModelCatalog {

const std::vector<ModelInfo>& allModels() {
    return catalog();
}

std::vector<ModelInfo> modelsForCategory(ModelCategory category) {
    std::vector<ModelInfo> result;
    std::copy_if(catalog().begin(), catalog().end(), std::back_inserter(result),
                 [category](const ModelInfo& info) { return info.category == category; });
    return result;
}

const ModelInfo* findByFilename(const QString& filename) {
    const auto& models = catalog();
    const auto it = std::find_if(models.begin(), models.end(), [&filename](const ModelInfo& info) {
        return info.filename == filename;
    });
    return it == models.end() ? nullptr : &(*it);
}

QString defaultFilename(ModelCategory category) {
    return category == ModelCategory::Segmentation
               ? QStringLiteral("BiRefNet-lite-F16.gguf")
               : QStringLiteral("ESRGAN-4x-foolhardy_Remacri-F16.gguf");
}

QString settingsKey(ModelCategory category) {
    return category == ModelCategory::Segmentation ? QStringLiteral("models/segmentationModel")
                                                     : QStringLiteral("models/upscaleModel");
}

} // namespace ModelCatalog
