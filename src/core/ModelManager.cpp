#include "ModelManager.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include <gguf.h>

#include <memory>

ModelManager::ModelManager(QString buildDefaultsDir, QObject* parent)
    : QObject(parent), buildDefaultsDir_(std::move(buildDefaultsDir)),
      network_(new QNetworkAccessManager(this)) {
    network_->setTransferTimeout(30000);
}

QString ModelManager::modelsDir() const {
    // Hand-built, not AppDataLocation: that enum hardcodes an <org>/<app>
    // nesting on Unix. The org name stays set for QSettings.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                         QLatin1Char('/') + QCoreApplication::applicationName();
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

QString ModelManager::readArchitecture(const QString& filePath) const {
    // Metadata-only read: with ctx == nullptr and no_alloc set, gguf parses
    // the header and KV pairs but never touches tensor data, so this stays
    // cheap even for multi-GB checkpoints.
    gguf_init_params params{};
    params.no_alloc = true;
    params.ctx = nullptr;
    gguf_context* context = gguf_init_from_file(filePath.toLocal8Bit().constData(), params);
    if (!context) {
        return {};
    }
    QString architecture;
    const int64_t key = gguf_find_key(context, "general.architecture");
    if (key >= 0 && gguf_get_kv_type(context, key) == GGUF_TYPE_STRING) {
        architecture = QString::fromUtf8(gguf_get_val_str(context, key));
    }
    gguf_free(context);
    return architecture;
}

std::vector<ScannedModel> ModelManager::scanModels() const {
    std::vector<ScannedModel> result;

    QSet<QString> catalogNames;
    for (const ModelInfo& info : ModelCatalog::allModels()) {
        catalogNames.insert(info.filename);
    }

    QDir dir(modelsDir());
    const QStringList files =
        dir.entryList({QStringLiteral("*.gguf")}, QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        if (catalogNames.contains(fileName)) {
            continue; // already listed by the curated catalog
        }
        ScannedModel model;
        model.info.filename = fileName;
        model.info.displayName = fileName;
        model.info.license = QStringLiteral("user-provided (license not verified)");
        model.info.approxSizeBytes = QFileInfo(dir.filePath(fileName)).size();
        const QString architecture = readArchitecture(dir.filePath(fileName));
        if (auto category = ModelCatalog::categoryForArchitecture(architecture)) {
            model.info.category = *category;
            model.usable = true;
        } else {
            model.usable = false;
            model.note = architecture.isEmpty()
                ? QStringLiteral("Not a readable GGUF file")
                : QStringLiteral("Unsupported architecture: %1").arg(architecture);
        }
        result.push_back(std::move(model));
    }
    return result;
}

bool ModelManager::importModel(const QString& sourcePath, QString* errorMessage) {
    auto fail = [errorMessage](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    const QString architecture = readArchitecture(sourcePath);
    if (!ModelCatalog::categoryForArchitecture(architecture)) {
        return fail(architecture.isEmpty()
            ? QStringLiteral("The file is not a readable GGUF model.")
            : QStringLiteral(
                  "Unsupported model architecture '%1'. This app can load %2 models.")
                  .arg(architecture, QStringLiteral("birefnet, scunet and esrgan")));
    }

    const QString fileName = QFileInfo(sourcePath).fileName();
    if (QFile::exists(pathFor(fileName))) {
        return fail(QStringLiteral("A model named '%1' already exists. Rename the file and "
                                    "try again.")
                        .arg(fileName));
    }
    if (!QFile::copy(sourcePath, pathFor(fileName))) {
        return fail(QStringLiteral("Could not copy the file into the models directory."));
    }
    return true;
}

void ModelManager::ensureDefaultsProvisioned() {
    if (buildDefaultsDir_.isEmpty()) {
        return;
    }
    for (ModelCategory category :
         {ModelCategory::Segmentation, ModelCategory::Denoise, ModelCategory::Upscale}) {
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
