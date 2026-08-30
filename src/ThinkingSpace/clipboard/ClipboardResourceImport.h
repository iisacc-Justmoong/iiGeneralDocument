#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVariantMap>

namespace ThinkingSpace::Clipboard
{
    struct ClipboardResourceImport final
    {
        QString fileName;
        QString localFilePath;
        QString mimeType;
        QString format;
        QString type;
        QString bucket;
        QByteArray payloadBytes;
        QImage image;

        bool valid() const noexcept;
        bool hasLocalFile() const noexcept;
        bool hasMemoryPayload() const noexcept;
        QVariantMap toVariantMap() const;
    };

    ClipboardResourceImport resourceImportForFileType(
        const QString& fileName,
        const QString& mimeType = QString());
    ClipboardResourceImport resourceImportForLocalFile(
        const QString& localFilePath,
        const QString& mimeType = QString());
    ClipboardResourceImport resourceImportForImage(
        const QImage& image,
        const QString& mimeType = QStringLiteral("image/png"));
    ClipboardResourceImport resourceImportForBytes(
        const QByteArray& bytes,
        const QString& fileName,
        const QString& mimeType);
} // namespace ThinkingSpace::Clipboard

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
