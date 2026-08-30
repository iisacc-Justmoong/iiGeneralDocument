#include "ThinkingSpace/clipboard/ClipboardResourceImport.h"

#include "ThinkingSpace/clipboard/FiletypeCapture.h"
#include "ThinkingSpace/file/hub/ThinkingSpaceHubPathUtils.hpp"
#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcePackageSupport.hpp"

#include <QFileInfo>

namespace
{
    void applyResourceTaxonomy(ThinkingSpace::Clipboard::ClipboardResourceImport* resourceImport)
    {
        if (resourceImport == nullptr)
        {
            return;
        }

        resourceImport->format = ThinkingSpace::Resources::normalizeFormat(resourceImport->format).toCaseFolded();
        if (resourceImport->format.trimmed().isEmpty())
        {
            resourceImport->format = QStringLiteral(".bin");
        }
        resourceImport->type = ThinkingSpace::Resources::inferTypeFromFormat(resourceImport->format);
        resourceImport->bucket = ThinkingSpace::Resources::inferBucket(resourceImport->type, resourceImport->format);
        resourceImport->fileName =
            ThinkingSpace::Clipboard::FiletypeCapture::normalizedFileNameOrDefault(
                resourceImport->fileName,
                resourceImport->format);
    }
} // namespace

namespace ThinkingSpace::Clipboard
{
    bool ClipboardResourceImport::valid() const noexcept
    {
        return !format.trimmed().isEmpty()
            && !type.trimmed().isEmpty();
    }

    bool ClipboardResourceImport::hasLocalFile() const noexcept
    {
        return !localFilePath.trimmed().isEmpty();
    }

    bool ClipboardResourceImport::hasMemoryPayload() const noexcept
    {
        return !payloadBytes.isEmpty() || !image.isNull();
    }

    QVariantMap ClipboardResourceImport::toVariantMap() const
    {
        QVariantMap entry;
        entry.insert(QStringLiteral("fileName"), fileName.trimmed());
        entry.insert(QStringLiteral("localFilePath"), localFilePath.trimmed());
        entry.insert(QStringLiteral("mimeType"), mimeType.trimmed());
        entry.insert(QStringLiteral("format"), Resources::normalizeFormat(format).toCaseFolded());
        entry.insert(QStringLiteral("type"), type.trimmed().toCaseFolded());
        entry.insert(QStringLiteral("bucket"), bucket.trimmed());
        entry.insert(QStringLiteral("hasPayloadBytes"), !payloadBytes.isEmpty());
        entry.insert(QStringLiteral("hasImage"), !image.isNull());
        return entry;
    }

    ClipboardResourceImport resourceImportForFileType(const QString& fileName, const QString& mimeType)
    {
        ClipboardResourceImport resourceImport;
        resourceImport.fileName = fileName;
        resourceImport.mimeType = FiletypeCapture::normalizeMimeType(mimeType);
        resourceImport.format = FiletypeCapture::normalizedFormatForFileType(fileName, mimeType);
        applyResourceTaxonomy(&resourceImport);
        return resourceImport;
    }

    ClipboardResourceImport resourceImportForLocalFile(const QString& localFilePath, const QString& mimeType)
    {
        ClipboardResourceImport resourceImport = resourceImportForFileType(QFileInfo(localFilePath).fileName(), mimeType);
        resourceImport.localFilePath = ThinkingSpace::HubPath::normalizeAbsolutePath(localFilePath);
        return resourceImport;
    }

    ClipboardResourceImport resourceImportForImage(const QImage& image, const QString& mimeType)
    {
        ClipboardResourceImport resourceImport = resourceImportForFileType(QString(), mimeType);
        if (resourceImport.type != QStringLiteral("image"))
        {
            resourceImport.mimeType = QStringLiteral("image/png");
            resourceImport.format = QStringLiteral(".png");
            applyResourceTaxonomy(&resourceImport);
        }
        resourceImport.image = image;
        return resourceImport;
    }

    ClipboardResourceImport resourceImportForBytes(
        const QByteArray& bytes,
        const QString& fileName,
        const QString& mimeType)
    {
        ClipboardResourceImport resourceImport = resourceImportForFileType(fileName, mimeType);
        resourceImport.payloadBytes = bytes;
        return resourceImport;
    }
} // namespace ThinkingSpace::Clipboard
