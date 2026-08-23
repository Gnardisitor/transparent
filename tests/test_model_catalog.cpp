#include <QtTest>

#include "core/ModelCatalog.h"

#include <QRegularExpression>
#include <QSet>

class TestModelCatalog : public QObject {
    Q_OBJECT

private slots:
    void everyEntryHasRequiredFields();
    void filenamesAreUnique();
    void checksumsLookLikeSha256();
    void modelsForCategoryOnlyReturnsThatCategory();
    void findByFilenameFindsKnownAndRejectsUnknown();
    void defaultsAreInTheCatalog();
};

void TestModelCatalog::everyEntryHasRequiredFields() {
    for (const ModelInfo& info : ModelCatalog::allModels()) {
        QVERIFY(!info.displayName.isEmpty());
        QVERIFY(!info.filename.isEmpty());
        QVERIFY(!info.license.isEmpty());
        QVERIFY(!info.url.isEmpty());
        QVERIFY(!info.sha256.isEmpty());
        QVERIFY(info.approxSizeBytes > 0);
    }
}

void TestModelCatalog::filenamesAreUnique() {
    // Installed-detection is pure filename matching (see ModelManager), so
    // a duplicate filename across two catalog entries would make them
    // indistinguishable on disk.
    QSet<QString> seen;
    for (const ModelInfo& info : ModelCatalog::allModels()) {
        QVERIFY(!seen.contains(info.filename));
        seen.insert(info.filename);
    }
}

void TestModelCatalog::checksumsLookLikeSha256() {
    static const QRegularExpression hexPattern(QStringLiteral("^[0-9a-f]{64}$"));
    for (const ModelInfo& info : ModelCatalog::allModels()) {
        QVERIFY2(hexPattern.match(info.sha256).hasMatch(),
                 qPrintable(QStringLiteral("%1: %2").arg(info.filename, info.sha256)));
    }
}

void TestModelCatalog::modelsForCategoryOnlyReturnsThatCategory() {
    const auto segmentation = ModelCatalog::modelsForCategory(ModelCategory::Segmentation);
    QVERIFY(!segmentation.empty());
    for (const ModelInfo& info : segmentation) {
        QVERIFY(info.category == ModelCategory::Segmentation);
    }

    const auto upscale = ModelCatalog::modelsForCategory(ModelCategory::Upscale);
    QVERIFY(!upscale.empty());
    for (const ModelInfo& info : upscale) {
        QVERIFY(info.category == ModelCategory::Upscale);
    }
}

void TestModelCatalog::findByFilenameFindsKnownAndRejectsUnknown() {
    const ModelInfo* found = ModelCatalog::findByFilename(QStringLiteral("BiRefNet-lite-F16.gguf"));
    QVERIFY(found != nullptr);
    QCOMPARE(found->filename, QStringLiteral("BiRefNet-lite-F16.gguf"));

    QVERIFY(ModelCatalog::findByFilename(QStringLiteral("nonexistent.gguf")) == nullptr);
}

void TestModelCatalog::defaultsAreInTheCatalog() {
    // main.cpp falls back to these filenames whenever nothing is persisted
    // in QSettings yet — a default that isn't itself a real catalog entry
    // would silently fail to load on first run.
    QVERIFY(ModelCatalog::findByFilename(
                ModelCatalog::defaultFilename(ModelCategory::Segmentation)) != nullptr);
    QVERIFY(ModelCatalog::findByFilename(ModelCatalog::defaultFilename(ModelCategory::Upscale)) !=
            nullptr);
}

QTEST_MAIN(TestModelCatalog)
#include "test_model_catalog.moc"
