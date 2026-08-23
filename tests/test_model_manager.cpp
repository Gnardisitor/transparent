#include <QtTest>

#include "core/ModelCatalog.h"
#include "core/ModelManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

// Only the synchronous, network-free surface (paths, install detection,
// checksum verification, default provisioning) is covered here —
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
};

void TestModelManager::initTestCase() {
    // Redirects QStandardPaths::AppDataLocation (and friends) into a
    // temporary, per-process test location instead of a developer's real
    // config/data directory — ModelManager must never touch actual
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

    for (ModelCategory category : {ModelCategory::Segmentation, ModelCategory::Upscale}) {
        QFile file(buildDir.filePath(ModelCatalog::defaultFilename(category)));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("fake weights");
    }

    ModelManager manager(buildDir.path());
    QVERIFY(!manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Segmentation)));
    QVERIFY(!manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Upscale)));

    manager.ensureDefaultsProvisioned();

    QVERIFY(manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Segmentation)));
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

void TestModelManager::ensureDefaultsProvisionedIsNoOpWithEmptyBuildDir() {
    // Empty buildDefaultsDir_ is the constructor's documented "no-op" case
    // (e.g. what tests pass) — it must not crash or fabricate paths.
    ModelManager manager{QString()};
    manager.ensureDefaultsProvisioned();
    QVERIFY(!manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Segmentation)));
    QVERIFY(!manager.isInstalled(ModelCatalog::defaultFilename(ModelCategory::Upscale)));
}

QTEST_MAIN(TestModelManager)
#include "test_model_manager.moc"
