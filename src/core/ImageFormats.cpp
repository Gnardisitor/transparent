#include "ImageFormats.h"

#include <QFileInfo>

namespace ImageFormats {

QStringList supportedExtensions() {
    return {"png", "jpg", "jpeg", "bmp", "webp", "gif"};
}

bool isSupported(const QString& filePath) {
    return supportedExtensions().contains(QFileInfo(filePath).suffix().toLower());
}

} // namespace ImageFormats
