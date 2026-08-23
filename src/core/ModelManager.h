#pragma once

#include "ModelCatalog.h"

#include <QObject>
#include <QSet>
#include <QString>

class QNetworkAccessManager;

// Owns the on-disk state behind model swapping: where curated models live
// (QStandardPaths::AppDataLocation, not the build tree, so downloads or
// manually placed files survive a rebuild), which of them are already
// installed, and downloading a missing one on demand with checksum
// verification. See PLAN.md's "Model management" section for the full
// design this implements.
class ModelManager : public QObject {
    Q_OBJECT

public:
    // `buildDefaultsDir` is TRANSPARENT_MODELS_DIR: the build-tree directory
    // CMake already downloads the two default models into at configure
    // time. Only ensureDefaultsProvisioned() uses it, to give a fresh build
    // a working first run with no network access. Empty is fine (e.g. in
    // tests) — ensureDefaultsProvisioned() is then a no-op.
    explicit ModelManager(QString buildDefaultsDir, QObject* parent = nullptr);

    // Directory models actually live in at runtime (created if missing).
    QString modelsDir() const;
    QString pathFor(const QString& filename) const;
    // Installed-detection is pure filename matching against modelsDir(), so
    // a file placed there by hand is picked up exactly like a downloaded
    // one — no separate "manual" bookkeeping.
    bool isInstalled(const QString& filename) const;

    // Copies the CMake-time-downloaded defaults (BiRefNet-lite,
    // foolhardy_Remacri) from buildDefaultsDir into modelsDir() if they
    // aren't already there: a local file copy, not a download, so a fresh
    // build still works fully offline on first run.
    void ensureDefaultsProvisioned();

    // True if `filePath`'s contents hash to `expectedSha256Hex` (hex,
    // case-insensitive, as published alongside each model).
    bool verifyChecksum(const QString& filePath, const QString& expectedSha256Hex) const;

    bool isDownloading(const QString& filename) const;

    // Starts an async download of `info` into modelsDir(), verified against
    // info.sha256 once complete. Never blocks. No-op if `info` is already
    // installed or already downloading. Progress/completion surface via the
    // signals below.
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
