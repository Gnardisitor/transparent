#pragma once

// Test helper: builds a minimal but structurally valid GGUF v3 file with one
// string KV ("general.architecture") and zero tensors, so metadata readers
// (gguf_init_from_file, visp's model_file) accept it without real weights.

#include <QFile>
#include <QString>

#include <cstdint>

namespace TestGguf {

inline bool writeTestGguf(const QString& path, const QString& architecture) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    auto writeU32 = [&file](uint32_t value) {
        file.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    auto writeU64 = [&file](uint64_t value) {
        file.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    auto writeString = [&](const QByteArray& value) {
        writeU64(static_cast<uint64_t>(value.size()));
        file.write(value);
    };

    file.write("GGUF", 4); // magic
    writeU32(3);           // version
    writeU64(0);           // tensor count
    writeU64(1);           // kv count
    writeString(QByteArrayLiteral("general.architecture"));
    writeU32(8); // GGUF_TYPE_STRING
    writeString(architecture.toUtf8());
    return true;
}

} // namespace TestGguf
