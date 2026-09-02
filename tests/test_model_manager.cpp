#include <QtTest>

#include "TestGguf.h"
#include "core/ModelCatalog.h"
#include "core/ModelManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>

// Only the synchronous, network-free surface (paths, install detection,
// checksum verification, default provisioning) is covered here;
// downloadModel() drives a real QNetworkAccessManager request and is
// exercised manually/in the running app instead, not against the network in
// CI.
class TestModelManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void modelsDirIsCreatedUnderAppData();
    void isInstalledReflectsActualFileState();
    void verifyChecksumAcceptsMatchAndRejectsMismatchAndMissingFile();
    void ensureDefaultsProvisionedCopiesFromBuildDir();
    void ensureDefaultsProvisionedSkipsAlreadyInstalled();
    void ensureDefaultsProvisionedIsNoOpWithEmptyBuildDir();
    void scanModelsClassifiesCustomGgufsByArchitecture();
    void scanModelsSkipsCatalogEntriesAndUnreadableFiles();
    void importModelCopiesValidFilesIntoTheModelsDir();
    void importModelRejectsUnrecognizedArchAndCollisions();
};

void TestModelManager::initTestCase() {
    // Redirects QStandardPaths::AppDataLocation (and friends) into a
    // temporary, per-process test location instead of a developer's real
    // config/data directory; ModelManager must never touch actual
    // installed models while under test.
    QStandardPaths::setTestModeEnabled(true);
}

void TestModelManager::cleanup() {
    // Every test shares the same test-mode AppData directory
    // (setTestModeEnabled computes one deterministic path, not a fresh one
    // per ModelManager),
    // so each test's leftover files are wiped afterward to keep them
    // isolated from each other.
    ModelManager manager{QString()};
    QDir dir(manager.modelsDir());
    for (const QString& entry : dir.entryList(QDir::Files)) {
        dir.remove(entry);
    }
}

void TestModelManager::modelsDirIsCreatedUnderAppData() {
    ModelManager manager{QString()};
    const QString dir = manager.modelsDir();
    QVERIFY(QDir(dir).exists());
    QVERIFY(dir.endsWith(QStringLiteral("/models")));
}

void TestModelManager::isInstalledReflectsActualFileState() {
    ModelManager manager{QString()};
    const QString filename = QStringLiteral("some-model.gguf");
    QVERIFY(!manager.isInstalled(filename));

    QFile file(manager.pathFor(filename));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("fake weights");
    file.close();

    QVERIFY(manager.isInstalled(filename));
}

void TestModelManager::verifyChecksumAcceptsMatchAndRejectsMismatchAndMissingFile() {
    ModelManager manager{QString()};
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("data.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("known content");
    file.close();

    const QString expectedHex = QString::fromLatin1(
        QCryptographicHash::hash(QByteArrayLiteral("known content"), QCryptographicHash::Sha256)
            .toHex());

    QVERIFY(manager.verifyChecksum(path, expectedHex));
    // Published checksums vary in case; verification shouldn't care.
    QVERIFY(manager.verifyChecksum(path, expectedHex.toUpper()));
    QVERIFY(!manager.verifyChecksum(
        path, QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000")));
    QVERIFY(!manager.verifyChecksum(dir.filePath(QStringLiteral("missing.bin")), expectedHex));
}

void TestModelManager::ensureDefaultsProvisionedCopiesFromBuildDir() {
    QTemporaryDir buildDir;
    QVERIFY(buildDir.isValid());

    for (ModelCategory category :
         {ModelCategory::Segmentation, ModelCategory::Denoise, ModelCategory::Upscale}) {
        QFile file(buildDir.filePath(ModelCatalog::defaultFilename(category)));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("fake weights");
    }

    ModelManager manager(buildDir.path());
    QVERIFY(!manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Segmentation)));
    QVERIFY(!manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Denoise)));
    QVERIFY(!manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Upscale)));

    manager.ensureDefaultsProvisioned();

    QVERIFY(manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Segmentation)));
    QVERIFY(manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Denoise)));
    QVERIFY(manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Upscale)));
}

void TestModelManager::ensureDefaultsProvisionedSkipsAlreadyInstalled() {
    ModelManager manager{QString()};
    const QString filename = ModelCatalog::defaultFilename(ModelCategory::Segmentation);

    QFile existing(manager.pathFor(filename));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("already here, not the build-dir copy");
    existing.close();

    QTemporaryDir buildDir;
    QVERIFY(buildDir.isValid());
    QFile buildFile(buildDir.filePath(filename));
    QVERIFY(buildFile.open(QIODevice::WriteOnly));
    buildFile.write("build-dir version");
    buildFile.close();

    ModelManager provisioning(buildDir.path());
    provisioning.ensureDefaultsProvisioned();

    QFile stillThere(manager.pathFor(filename));
    QVERIFY(stillThere.open(QIODevice::ReadOnly));
    QCOMPARE(stillThere.readAll(), QByteArrayLiteral("already here, not the build-dir copy"));
}

void TestModelManager::scanModelsClassifiesCustomGgufsByArchitecture() {
    ModelManager manager{QString()};
    QVERIFY(TestGguf::writeTestGguf(manager.pathFor(QStringLiteral("my-birefnet.gguf")),
                                    QStringLiteral("birefnet")));
    QVERIFY(TestGguf::writeTestGguf(manager.pathFor(QStringLiteral("my-scunet.gguf")),
                                    QStringLiteral("scunet")));
    QVERIFY(TestGguf::writeTestGguf(manager.pathFor(QStringLiteral("my-esrgan.gguf")),
                                    QStringLiteral("esrgan")));
    // Known to vision.cpp but without a loading seam in this app.
    QVERIFY(TestGguf::writeTestGguf(manager.pathFor(QStringLiteral("my-migan.gguf")),
                                    QStringLiteral("migan")));
    // Unrecognized architecture.
    QVERIFY(TestGguf::writeTestGguf(manager.pathFor(QStringLiteral("rmbg-1.4.gguf")),
                                    QStringLiteral("rmbg")));

    const std::vector<ScannedModel> scanned = manager.scanModels();
    QCOMPARE(static_cast<int>(scanned.size()), 5);

    std::vector<const ScannedModel*> byName;
    for (const auto& model : scanned) {
        byName.push_back(&model);
        // Sorted by filename; display name is the filename; license is
        // explicitly unverified.
        QCOMPARE(model.info.displayName, model.info.filename);
        QCOMPARE(model.info.license, QStringLiteral("user-provided (license not verified)"));
    }
    QVERIFY(std::is_sorted(byName.begin(), byName.end(),
                           [](const auto* a, const auto* b) {
                               return a->info.filename < b->info.filename;
                           }));

    auto find = [&byName](const char* name) {
        auto it = std::find_if(byName.begin(), byName.end(), [name](const auto* model) {
            return model->info.filename == QLatin1String(name);
        });
        return it != byName.end() ? *it : nullptr;
    };

    const auto* birefnet = find("my-birefnet.gguf");
    QVERIFY(birefnet && birefnet->usable);
    QCOMPARE(birefnet->info.category, ModelCategory::Segmentation);
    const auto* scunet = find("my-scunet.gguf");
    QVERIFY(scunet && scunet->usable);
    QCOMPARE(scunet->info.category, ModelCategory::Denoise);
    const auto* esrgan = find("my-esrgan.gguf");
    QVERIFY(esrgan && esrgan->usable);
    QCOMPARE(esrgan->info.category, ModelCategory::Upscale);

    // Recognized but seam-less arches, and unknown arches, are unusable with
    // an explanatory note.
    const auto* migan = find("my-migan.gguf");
    QVERIFY(migan && !migan->usable);
    QVERIFY(migan->note.contains(QStringLiteral("migan")));
    const auto* rmbg = find("rmbg-1.4.gguf");
    QVERIFY(rmbg && !rmbg->usable);
    QVERIFY(rmbg->note.contains(QStringLiteral("rmbg")));
}

void TestModelManager::scanModelsSkipsCatalogEntriesAndUnreadableFiles() {
    ModelManager manager{QString()};
    // A catalog-named file must not reappear as a custom model.
    QVERIFY(TestGguf::writeTestGguf(
        manager.pathFor(ModelCatalog::defaultFilename(ModelCategory::Segmentation)),
        QStringLiteral("birefnet")));
    // A non-GGUF file: unusable with a note, not a crash.
    QFile junk(manager.pathFor(QStringLiteral("not-a-model.gguf")));
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("this is not a gguf");

    const std::vector<ScannedModel> scanned = manager.scanModels();
    QVERIFY(std::none_of(scanned.begin(), scanned.end(), [](const auto& model) {
        return model.info.filename == ModelCatalog::defaultFilename(ModelCategory::Segmentation);
    }));

    auto junkIt = std::find_if(scanned.begin(), scanned.end(), [](const auto& model) {
        return model.info.filename == QLatin1String("not-a-model.gguf");
    });
    QVERIFY(junkIt != scanned.end());
    QVERIFY(!junkIt->usable);
    QVERIFY(!junkIt->note.isEmpty());
}

void TestModelManager::importModelCopiesValidFilesIntoTheModelsDir() {
    QTemporaryDir sourceDir;
    QVERIFY(sourceDir.isValid());
    const QString source = sourceDir.filePath(QStringLiteral("custom-scunet.gguf"));
    QVERIFY(TestGguf::writeTestGguf(source, QStringLiteral("scunet")));

    ModelManager manager{QString()};
    QString error;
    // No category argument: the architecture routes the file to its section.
    QVERIFY(manager.importModel(source, &error));
    QVERIFY(error.isEmpty());

    // The copy is installed and scans as a usable Denoise model; the source
    // file is left untouched.
    QVERIFY(manager.isInstalled(QStringLiteral("custom-scunet.gguf")));
    QVERIFY(QFile::exists(source));
    const auto scanned = manager.scanModels();
    auto it = std::find_if(scanned.begin(), scanned.end(), [](const auto& model) {
        return model.info.filename == QLatin1String("custom-scunet.gguf");
    });
    QVERIFY(it != scanned.end());
    QCOMPARE(it->info.category, ModelCategory::Denoise);
    QVERIFY(it->usable);
}

void TestModelManager::importModelRejectsUnrecognizedArchAndCollisions() {
    QTemporaryDir sourceDir;
    QVERIFY(sourceDir.isValid());
    const QString birefnetFile = sourceDir.filePath(QStringLiteral("some-birefnet.gguf"));
    QVERIFY(TestGguf::writeTestGguf(birefnetFile, QStringLiteral("birefnet")));
    const QString rmbgFile = sourceDir.filePath(QStringLiteral("some-rmbg.gguf"));
    QVERIFY(TestGguf::writeTestGguf(rmbgFile, QStringLiteral("rmbg")));
    const QString junkFile = sourceDir.filePath(QStringLiteral("junk.gguf"));
    {
        QFile f(junkFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("junk");
    }

    ModelManager manager{QString()};
    QString error;

    // Recognized by vision.cpp but with no seam in this app.
    QVERIFY(!manager.importModel(rmbgFile, &error));
    QVERIFY(error.contains(QStringLiteral("rmbg")));
    QVERIFY(!manager.isInstalled(QStringLiteral("some-rmbg.gguf")));

    // Unreadable file.
    QVERIFY(!manager.importModel(junkFile, &error));
    QVERIFY(!error.isEmpty());

    // A supported file imports and re-importing it collides on the name.
    QVERIFY(manager.importModel(birefnetFile, &error));
    QVERIFY(!manager.importModel(birefnetFile, &error));
    QVERIFY(error.contains(QStringLiteral("already exists")));
}

void TestModelManager::ensureDefaultsProvisionedIsNoOpWithEmptyBuildDir() {
    // Empty buildDefaultsDir_ is the constructor's documented "no-op" case
    // (e.g. what tests pass); it must not crash or fabricate paths.
    ModelManager manager{QString()};
    manager.ensureDefaultsProvisioned();
    QVERIFY(!manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Segmentation)));
    QVERIFY(!manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Upscale)));
}

QTEST_MAIN(TestModelManager)
#include "test_model_manager.moc"
