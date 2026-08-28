#pragma once

#include "ModelCatalog.h"

#include <QObject>
#include <QSet>
#include <QString>

class QNetworkAccessManager;

// Owns the on-disk state behind model swapping: models live in
// QStandardPaths::AppDataLocation (surviving rebuilds), installation state,
// and on-demand downloads with checksum verification.
class ModelManager : public QObject {
    Q_OBJECT

public:
    // `buildDefaultsDir` is the build-tree dir CMake downloads the two
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
    QNetworkAccessManager* network_;
    QSet<QString> activeDownloads_;
};
