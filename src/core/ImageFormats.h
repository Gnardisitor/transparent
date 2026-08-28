#pragma once

#include <QString>
#include <QStringList>

// The one extension list shared by drop handling and batch scanning.
namespace ImageFormats {

QStringList supportedExtensions();

// True if filePath's suffix (case-insensitive) is one of supportedExtensions().
bool isSupported(const QString& filePath);

} // namespace ImageFormats
