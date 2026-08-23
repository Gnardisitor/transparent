#include "ModelManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include <memory>

ModelManager::ModelManager(QString buildDefaultsDir, QObject* parent)
    : QObject(parent), buildDefaultsDir_(std::move(buildDefaultsDir)),
      network_(new QNetworkAccessManager(this)) {}

QString ModelManager::modelsDir() const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dir = base + QStringLiteral("/models");
    QDir().mkpath(dir);
    return dir;
}

QString ModelManager::pathFor(const QString& filename) const {
    return modelsDir() + QLatin1Char('/') + filename;
}

bool ModelManager::isInstalled(const QString& filename) const {
    return QFile::exists(pathFor(filename));
}

void ModelManager::ensureDefaultsProvisioned() {
    if (buildDefaultsDir_.isEmpty()) {
        return;
    }
    for (ModelCategory category : {ModelCategory::Segmentation, ModelCategory::Upscale}) {
        const QString filename = ModelCatalog::defaultFilename(category);
        if (isInstalled(filename)) {
            continue;
        }
        const QString source = buildDefaultsDir_ + QLatin1Char('/') + filename;
        if (QFile::exists(source)) {
            QFile::copy(source, pathFor(filename));
        }
    }
}

bool ModelManager::verifyChecksum(const QString& filePath, const QString& expectedSha256Hex) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return false;
    }
    return QString::fromLatin1(hash.result().toHex())
               .compare(expectedSha256Hex, Qt::CaseInsensitive) == 0;
}

bool ModelManager::isDownloading(const QString& filename) const {
    return activeDownloads_.contains(filename);
}

void ModelManager::downloadModel(const ModelInfo& info) {
    if (isInstalled(info.filename) || isDownloading(info.filename)) {
        return;
    }

    const QString partPath = pathFor(info.filename) + QStringLiteral(".part");
    auto file = std::make_shared<QFile>(partPath);
    if (!file->open(QIODevice::WriteOnly)) {
        emit downloadFinished(info.filename, false,
                               QStringLiteral("Could not write to %1").arg(partPath));
        return;
    }

    QNetworkReply* reply = network_->get(QNetworkRequest(QUrl(info.url)));
    activeDownloads_.insert(info.filename);

    // QNetworkReply doesn't redeclare readyRead itself, it's inherited from
    // QIODevice, so the pointer-to-member has to name the base class here.
    connect(reply, &QIODevice::readyRead, this, [reply, file]() { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, filename = info.filename](qint64 received, qint64 total) {
                emit downloadProgress(filename, received, total);
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, info, partPath]() {
        activeDownloads_.remove(info.filename);
        file->write(reply->readAll());
        file->close();
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(partPath);
            emit downloadFinished(info.filename, false, reply->errorString());
            return;
        }

        if (!verifyChecksum(partPath, info.sha256)) {
            QFile::remove(partPath);
            emit downloadFinished(
                info.filename, false,
                QStringLiteral("Downloaded file didn't match the expected checksum"));
            return;
        }

        QFile::remove(pathFor(info.filename));
        if (!QFile::rename(partPath, pathFor(info.filename))) {
            QFile::remove(partPath);
            emit downloadFinished(info.filename, false, QStringLiteral("Could not finalize download"));
            return;
        }

        emit downloadFinished(info.filename, true, QString());
    });
}
