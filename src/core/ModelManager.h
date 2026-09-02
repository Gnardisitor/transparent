#pragma once

#include "ModelCatalog.h"

#include <QObject>
#include <QSet>
#include <QString>

#include <vector>

class QNetworkAccessManager;

// One user-provided model discovered by scanning the models directory.
struct ScannedModel {
    // filename and displayName are the filename; license is always
    // "user-provided (license not verified)"; url/sha256 are empty and
    // approxSizeBytes is the actual file size. category is only set when
    // usable.
    ModelInfo info{};
    // True when the GGUF architecture maps to a category seam in this app.
    bool usable = false;
    // For unusable files: why (named architecture or read failure); shown
    // as a tooltip in the settings UI.
    QString note;
};

// Owns the on-disk state behind model swapping: models live in
// QStandardPaths::AppDataLocation (surviving rebuilds), installation state,
// and on-demand downloads with checksum verification.
class ModelManager : public QObject {
    Q_OBJECT

public:
    // `buildDefaultsDir` is the build-tree dir CMake downloads the
    // default models into; only ensureDefaultsProvisioned() uses it. Empty
    // is fine (tests).
    explicit ModelManager(QString buildDefaultsDir, QObject* parent = nullptr);

    // Directory models actually live in at runtime (created if missing).
    QString modelsDir() const;
    QString pathFor(const QString& filename) const;
    // Pure filename matching, so manually placed files count as installed.
    bool isInstalled(const QString& filename) const;

    // Copies the CMake-time-downloaded defaults into modelsDir() if
    // missing, so a fresh build works offline on first run.
    void ensureDefaultsProvisioned();

    // True if `filePath`'s contents hash to `expectedSha256Hex` (hex,
    // case-insensitive).
    bool verifyChecksum(const QString& filePath, const QString& expectedSha256Hex) const;

    bool isDownloading(const QString& filename) const;

    // Folder-as-state: scans the models directory for .gguf files that are
    // not curated entries and classifies them by GGUF architecture. Reads
    // only file headers, never tensor data. Sorted by filename.
    std::vector<ScannedModel> scanModels() const;

    // Copies a user-provided .gguf into the models directory after checking
    // that its architecture is one this app can load. The directory scan
    // then routes it to its category automatically. Fails with a message
    // when the architecture is unrecognized, the file is unreadable, or a
    // file of the same name already exists. Returns true and copies on
    // success.
    bool importModel(const QString& sourcePath, QString* errorMessage = nullptr);

    // Async download into modelsDir(), verified against info.sha256. No-op
    // if already installed or downloading. Signals below report progress
    // and completion.
    void downloadModel(const ModelInfo& info);

signals:
    void downloadProgress(QString filename, qint64 bytesReceived, qint64 bytesTotal);
    // errorMessage is empty on success.
    void downloadFinished(QString filename, bool success, QString errorMessage);

private:
    QString buildDefaultsDir_;
    // Reads a GGUF file's `general.architecture` metadata without loading
    // tensor data; empty string when unreadable.
    QString readArchitecture(const QString& filePath) const;
    QNetworkAccessManager* network_;
    QSet<QString> activeDownloads_;
};
