#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"
#include "ThinkingSpace/file/hub/ThinkingSpaceHubPathUtils.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QBuffer>

namespace ThinkingSpace::Resources
{
    struct ResourcePackageMetadata final
    {
        QString resourceId;
        QString resourcePath;
        QString assetPath;
        QString annotationPath;
        QString bucket;
        QString type;
        QString format;
    };

    inline QString decodeXmlEntities(QString text)
    {
        text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
        text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        return text;
    }

    inline QString packageDirectorySuffix()
    {
        return QStringLiteral(".tsresource");
    }

    inline QString metadataFileName()
    {
        return QStringLiteral("resource.xml");
    }

    inline QString annotationFileName()
    {
        return QStringLiteral("annotation.png");
    }

    inline QString normalizePath(const QString& value)
    {
        return ThinkingSpace::HubPath::normalizePath(value);
    }

    inline bool isResourcePackageDirectoryName(const QString& value)
    {
        return value.trimmed().endsWith(packageDirectorySuffix(), Qt::CaseInsensitive);
    }

    inline QString resourceIdFromPackageName(const QString& packageName)
    {
        QString normalized = QFileInfo(packageName.trimmed()).completeBaseName().trimmed();
        if (normalized.endsWith(packageDirectorySuffix(), Qt::CaseInsensitive))
        {
            normalized.chop(packageDirectorySuffix().size());
        }
        return normalized.trimmed();
    }

    inline QString normalizeFormat(QString value)
    {
        value = decodeXmlEntities(value).trimmed();
        if (value.isEmpty())
        {
            return {};
        }
        if (!value.startsWith(QLatin1Char('.')))
        {
            value.prepend(QLatin1Char('.'));
        }

        const int lastDotIndex = value.lastIndexOf(QLatin1Char('.'));
        if (lastDotIndex > 0 && lastDotIndex < value.size() - 1)
        {
            value = value.mid(lastDotIndex);
        }
        return value;
    }

    inline QString normalizedFormatLookupKey(QString value)
    {
        return normalizeFormat(std::move(value)).toCaseFolded();
    }

    inline QString formatFromAssetFilePath(const QString& assetFilePath)
    {
        const QFileInfo fileInfo(assetFilePath.trimmed());
        const QString fileName = fileInfo.fileName().trimmed();
        if (fileName.isEmpty())
        {
            return {};
        }

        const int lastDotIndex = fileName.lastIndexOf(QLatin1Char('.'));
        if (lastDotIndex <= 0 || lastDotIndex == fileName.size() - 1)
        {
            return {};
        }

        return fileName.mid(lastDotIndex);
    }

    inline QString normalizedType(QString value)
    {
        value = decodeXmlEntities(value).trimmed().toCaseFolded();
        value.remove(QLatin1Char(' '));
        value.remove(QLatin1Char('-'));
        value.remove(QLatin1Char('_'));

        if (value == QStringLiteral("webpage") || value == QStringLiteral("weblink") || value == QStringLiteral("url"))
        {
            return QStringLiteral("link");
        }
        if (value == QStringLiteral("3dmodel"))
        {
            return QStringLiteral("model");
        }
        if (value == QStringLiteral("zip") || value == QStringLiteral("archive"))
        {
            return QStringLiteral("archive");
        }
        if (value == QStringLiteral("music") || value == QStringLiteral("sound"))
        {
            return QStringLiteral("audio");
        }
        return value;
    }

    inline QString inferTypeFromFormat(const QString& format)
    {
        static const QSet<QString> kImageFormats = {
            QStringLiteral(".png"),
            QStringLiteral(".jpeg"),
            QStringLiteral(".jpg"),
            QStringLiteral(".tiff"),
            QStringLiteral(".webp"),
            QStringLiteral(".gif"),
            QStringLiteral(".bmp"),
            QStringLiteral(".svg"),
            QStringLiteral(".heic"),
            QStringLiteral(".avif")
        };
        static const QSet<QString> kVideoFormats = {
            QStringLiteral(".mp4"),
            QStringLiteral(".mov"),
            QStringLiteral(".avi"),
            QStringLiteral(".mkv"),
            QStringLiteral(".webm"),
            QStringLiteral(".m4v")
        };
        static const QSet<QString> kDocumentFormats = {
            QStringLiteral(".pdf"),
            QStringLiteral(".doc"),
            QStringLiteral(".docx"),
            QStringLiteral(".txt"),
            QStringLiteral(".md"),
            QStringLiteral(".rtf"),
            QStringLiteral(".ppt"),
            QStringLiteral(".pptx"),
            QStringLiteral(".xls"),
            QStringLiteral(".xlsx"),
            QStringLiteral(".csv")
        };
        static const QSet<QString> kModelFormats = {
            QStringLiteral(".fbx"),
            QStringLiteral(".obj"),
            QStringLiteral(".gltf"),
            QStringLiteral(".glb"),
            QStringLiteral(".usd"),
            QStringLiteral(".usdz"),
            QStringLiteral(".blend"),
            QStringLiteral(".stl")
        };
        static const QSet<QString> kLinkFormats = {
            QStringLiteral(".html"),
            QStringLiteral(".htm"),
            QStringLiteral(".mhtml"),
            QStringLiteral(".webloc"),
            QStringLiteral(".url")
        };
        static const QSet<QString> kAudioFormats = {
            QStringLiteral(".mp3"),
            QStringLiteral(".aac"),
            QStringLiteral(".m4a"),
            QStringLiteral(".flac"),
            QStringLiteral(".alac"),
            QStringLiteral(".aiff"),
            QStringLiteral(".wav"),
            QStringLiteral(".ogg"),
            QStringLiteral(".opus"),
            QStringLiteral(".caf"),
            QStringLiteral(".wma"),
            QStringLiteral(".amr")
        };
        static const QSet<QString> kArchiveFormats = {
            QStringLiteral(".zip"),
            QStringLiteral(".rar"),
            QStringLiteral(".7z"),
            QStringLiteral(".tar"),
            QStringLiteral(".gz"),
            QStringLiteral(".bz2"),
            QStringLiteral(".xz")
        };

        const QString normalized = normalizedFormatLookupKey(format);
        QString terminalSuffix = normalized;
        const int lastDotIndex = normalized.lastIndexOf(QLatin1Char('.'));
        if (lastDotIndex > 0)
        {
            terminalSuffix = normalized.mid(lastDotIndex);
        }

        const auto matches = [&normalized, &terminalSuffix](const QSet<QString>& candidates)
        {
            return candidates.contains(normalized) || candidates.contains(terminalSuffix);
        };

        if (matches(kImageFormats))
        {
            return QStringLiteral("image");
        }
        if (matches(kVideoFormats))
        {
            return QStringLiteral("video");
        }
        if (matches(kDocumentFormats))
        {
            return QStringLiteral("document");
        }
        if (matches(kModelFormats))
        {
            return QStringLiteral("model");
        }
        if (matches(kLinkFormats))
        {
            return QStringLiteral("link");
        }
        if (matches(kAudioFormats))
        {
            return QStringLiteral("audio");
        }
        if (matches(kArchiveFormats))
        {
            return QStringLiteral("archive");
        }
        return QStringLiteral("other");
    }

    inline QString normalizeBucket(QString value)
    {
        value = decodeXmlEntities(value).trimmed();
        const QString folded = value.toCaseFolded();

        if (folded == QStringLiteral("image"))
        {
            return QStringLiteral("Image");
        }
        if (folded == QStringLiteral("video"))
        {
            return QStringLiteral("Video");
        }
        if (folded == QStringLiteral("document"))
        {
            return QStringLiteral("Document");
        }
        if (folded == QStringLiteral("3d model") || folded == QStringLiteral("3dmodel"))
        {
            return QStringLiteral("3D Model");
        }
        if (folded == QStringLiteral("web page") || folded == QStringLiteral("webpage") || folded == QStringLiteral("link"))
        {
            return QStringLiteral("Web page");
        }
        if (folded == QStringLiteral("audio")
            || folded == QStringLiteral("music")
            || folded == QStringLiteral("sound"))
        {
            return QStringLiteral("Audio");
        }
        if (folded == QStringLiteral("zip") || folded == QStringLiteral("archive"))
        {
            return QStringLiteral("ZIP");
        }
        if (folded == QStringLiteral("other"))
        {
            return QStringLiteral("Other");
        }

        return value;
    }

    inline QString inferBucket(const QString& type, const QString& format)
    {
        const QString normalizedTypeValue = normalizedType(type);
        if (normalizedTypeValue == QStringLiteral("image"))
        {
            return QStringLiteral("Image");
        }
        if (normalizedTypeValue == QStringLiteral("video"))
        {
            return QStringLiteral("Video");
        }
        if (normalizedTypeValue == QStringLiteral("document"))
        {
            return QStringLiteral("Document");
        }
        if (normalizedTypeValue == QStringLiteral("model"))
        {
            return QStringLiteral("3D Model");
        }
        if (normalizedTypeValue == QStringLiteral("link"))
        {
            return QStringLiteral("Web page");
        }
        if (normalizedTypeValue == QStringLiteral("audio"))
        {
            return QStringLiteral("Audio");
        }
        if (normalizedTypeValue == QStringLiteral("archive"))
        {
            return QStringLiteral("ZIP");
        }
        return normalizeBucket(inferTypeFromFormat(format));
    }

    inline QString inferBucketFromFormat(const QString& format)
    {
        return inferBucket(QString(), format);
    }

    inline QString normalizedBucketFromTypeAndFormat(const QString& bucket, const QString& type, const QString& format)
    {
        const QString normalizedBucketValue = normalizeBucket(bucket);
        if (!normalizedBucketValue.isEmpty())
        {
            return normalizedBucketValue;
        }
        return inferBucket(type, format);
    }

    inline QString normalizedTypeFromBucketAndFormat(const QString& type, const QString& bucket, const QString& format)
    {
        const QString normalizedTypeValue = normalizedType(type);
        if (!normalizedTypeValue.isEmpty())
        {
            return normalizedTypeValue;
        }

        const QString normalizedBucketValue = normalizeBucket(bucket);
        if (normalizedBucketValue == QStringLiteral("Image"))
        {
            return QStringLiteral("image");
        }
        if (normalizedBucketValue == QStringLiteral("Video"))
        {
            return QStringLiteral("video");
        }
        if (normalizedBucketValue == QStringLiteral("Document"))
        {
            return QStringLiteral("document");
        }
        if (normalizedBucketValue == QStringLiteral("3D Model"))
        {
            return QStringLiteral("model");
        }
        if (normalizedBucketValue == QStringLiteral("Web page"))
        {
            return QStringLiteral("link");
        }
        if (normalizedBucketValue == QStringLiteral("Audio"))
        {
            return QStringLiteral("audio");
        }
        if (normalizedBucketValue == QStringLiteral("ZIP"))
        {
            return QStringLiteral("archive");
        }
        return inferTypeFromFormat(format);
    }

    inline ResourcePackageMetadata buildMetadataForAssetFile(
        const QString& assetFilePath,
        const QString& resourceId = QString(),
        const QString& resourcePath = QString())
    {
        ResourcePackageMetadata metadata;
        metadata.resourceId = resourceId.trimmed();
        metadata.resourcePath = normalizePath(resourcePath);
        metadata.assetPath = QFileInfo(assetFilePath.trimmed()).fileName().trimmed();
        metadata.annotationPath = annotationFileName();
        metadata.format = formatFromAssetFilePath(assetFilePath);
        metadata.type = inferTypeFromFormat(metadata.format);
        metadata.bucket = inferBucket(metadata.type, metadata.format);
        return metadata;
    }

    inline QString metadataFilePathForPackage(const QString& packageDirectoryPath)
    {
        return QDir(packageDirectoryPath).filePath(metadataFileName());
    }

    inline QString annotationFilePathForPackage(const QString& packageDirectoryPath)
    {
        return QDir(packageDirectoryPath).filePath(annotationFileName());
    }

    inline QSize annotationBitmapSizeForAssetFile(const QString& assetFilePath)
    {
        const QString normalizedAssetFilePath = assetFilePath.trimmed();
        if (normalizedAssetFilePath.isEmpty())
        {
            return QSize(1, 1);
        }

        QImageReader imageReader(normalizedAssetFilePath);
        const QSize readerSize = imageReader.size();
        if (readerSize.isValid() && !readerSize.isEmpty())
        {
            return readerSize;
        }

        const QImage loadedImage(normalizedAssetFilePath);
        if (!loadedImage.isNull() && !loadedImage.size().isEmpty())
        {
            return loadedImage.size();
        }

        return QSize(1, 1);
    }

    inline QImage createEmptyAnnotationBitmap(const QString& assetFilePath = QString())
    {
        QSize bitmapSize = annotationBitmapSizeForAssetFile(assetFilePath);
        if (!bitmapSize.isValid() || bitmapSize.isEmpty())
        {
            bitmapSize = QSize(1, 1);
        }

        QImage image(bitmapSize, QImage::Format_ARGB32_Premultiplied);
        image.fill(0x00000000);
        return image;
    }

    inline QByteArray createEmptyAnnotationBitmapPngBytes(const QString& assetFilePath = QString())
    {
        const QImage image = createEmptyAnnotationBitmap(assetFilePath);
        if (image.isNull())
        {
            return {};
        }

        QByteArray encodedBytes;
        QBuffer buffer(&encodedBytes);
        if (!buffer.open(QIODevice::WriteOnly))
        {
            return {};
        }
        if (!image.save(&buffer, "PNG"))
        {
            return {};
        }
        return encodedBytes;
    }

    inline bool writeResourcePackageAnnotationBitmap(
        const QString& packageDirectoryPath,
        const QString& assetFilePath = QString(),
        QString* errorMessage = nullptr)
    {
        const QString normalizedPackageDirectoryPath = normalizePath(packageDirectoryPath);
        if (normalizedPackageDirectoryPath.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Package directory path is empty.");
            }
            return false;
        }

        if (!QDir().mkpath(normalizedPackageDirectoryPath))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to create resource package directory: %1").arg(
                    normalizedPackageDirectoryPath);
            }
            return false;
        }

        const QByteArray encodedBytes = createEmptyAnnotationBitmapPngBytes(assetFilePath);
        if (encodedBytes.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to encode annotation bitmap PNG for: %1").arg(assetFilePath);
            }
            return false;
        }

        QSaveFile annotationFile(annotationFilePathForPackage(normalizedPackageDirectoryPath));
        if (!annotationFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to open annotation bitmap for write: %1").arg(
                    annotationFile.fileName());
            }
            return false;
        }
        if (annotationFile.write(encodedBytes) != encodedBytes.size())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to write annotation bitmap: %1").arg(annotationFile.fileName());
            }
            annotationFile.cancelWriting();
            return false;
        }
        if (!annotationFile.commit())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to commit annotation bitmap: %1").arg(annotationFile.fileName());
            }
            return false;
        }
        return true;
    }

    inline QStringList directChildAssetCandidates(const QString& packageDirectoryPath)
    {
        const QDir packageDir(packageDirectoryPath);
        QStringList assetPaths;
        const QFileInfoList children = packageDir.entryInfoList(
            QDir::Files | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QFileInfo& child : children)
        {
            if (child.fileName().compare(metadataFileName(), Qt::CaseInsensitive) == 0)
            {
                continue;
            }
            if (child.fileName().compare(annotationFileName(), Qt::CaseInsensitive) == 0)
            {
                continue;
            }
            assetPaths.push_back(child.fileName());
        }
        return assetPaths;
    }

    inline void finalizeMetadata(
        ResourcePackageMetadata* metadata,
        const QString& packageDirectoryPath,
        const QString& fallbackResourcePath = QString())
    {
        if (metadata == nullptr)
        {
            return;
        }

        if (metadata->resourceId.trimmed().isEmpty())
        {
            metadata->resourceId = resourceIdFromPackageName(QFileInfo(packageDirectoryPath).fileName());
        }
        metadata->resourceId = metadata->resourceId.trimmed();

        if (metadata->resourcePath.trimmed().isEmpty())
        {
            metadata->resourcePath = fallbackResourcePath.trimmed();
        }
        metadata->resourcePath = normalizePath(metadata->resourcePath);

        metadata->assetPath = normalizePath(metadata->assetPath);
        metadata->annotationPath = normalizePath(metadata->annotationPath);
        if (metadata->assetPath.isEmpty())
        {
            const QStringList candidates = directChildAssetCandidates(packageDirectoryPath);
            if (candidates.size() == 1)
            {
                metadata->assetPath = candidates.constFirst();
            }
        }
        if (metadata->annotationPath.isEmpty())
        {
            metadata->annotationPath = annotationFileName();
        }

        if (metadata->format.trimmed().isEmpty())
        {
            metadata->format = formatFromAssetFilePath(metadata->assetPath);
        }
        metadata->format = normalizeFormat(metadata->format);
        if (metadata->format.isEmpty())
        {
            metadata->format = formatFromAssetFilePath(metadata->assetPath);
        }

        metadata->type = normalizedTypeFromBucketAndFormat(metadata->type, metadata->bucket, metadata->format);
        metadata->bucket = normalizedBucketFromTypeAndFormat(metadata->bucket, metadata->type, metadata->format);
    }

    inline QString createResourcePackageMetadataXml(ResourcePackageMetadata metadata)
    {
        metadata.format = normalizeFormat(metadata.format);
        metadata.type = normalizedTypeFromBucketAndFormat(metadata.type, metadata.bucket, metadata.format);
        metadata.bucket = normalizedBucketFromTypeAndFormat(metadata.bucket, metadata.type, metadata.format);

        QString text;
        QXmlStreamWriter writer(&text);
        writer.setAutoFormatting(true);
        writer.writeStartDocument();
        writer.writeStartElement(QStringLiteral("tsresource"));
        writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));
        writer.writeAttribute(QStringLiteral("schema"), QStringLiteral("thinkingspace.resource.package"));
        if (!metadata.resourceId.trimmed().isEmpty())
        {
            writer.writeAttribute(QStringLiteral("id"), metadata.resourceId.trimmed());
        }
        if (!metadata.resourcePath.trimmed().isEmpty())
        {
            writer.writeAttribute(QStringLiteral("resourcePath"), normalizePath(metadata.resourcePath));
        }
        if (!metadata.bucket.trimmed().isEmpty())
        {
            writer.writeAttribute(QStringLiteral("bucket"), metadata.bucket.trimmed());
        }
        if (!metadata.type.trimmed().isEmpty())
        {
            writer.writeAttribute(QStringLiteral("type"), metadata.type.trimmed());
        }
        if (!metadata.format.trimmed().isEmpty())
        {
            writer.writeAttribute(QStringLiteral("format"), metadata.format.trimmed());
        }
        writer.writeEmptyElement(QStringLiteral("annotation"));
        writer.writeAttribute(
            QStringLiteral("path"),
            normalizePath(metadata.annotationPath.trimmed().isEmpty()
                              ? annotationFileName()
                              : metadata.annotationPath));
        writer.writeEmptyElement(QStringLiteral("asset"));
        if (!metadata.assetPath.trimmed().isEmpty())
        {
            writer.writeAttribute(QStringLiteral("path"), normalizePath(metadata.assetPath));
        }
        writer.writeEndElement();
        writer.writeEndDocument();
        return text;
    }

    inline bool parseResourcePackageMetadataXml(
        const QString& rawText,
        ResourcePackageMetadata* outMetadata,
        QString* errorMessage = nullptr)
    {
        if (outMetadata == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("outMetadata must not be null.");
            }
            return false;
        }

        *outMetadata = {};
        QXmlStreamReader reader(rawText);
        bool rootFound = false;

        while (!reader.atEnd())
        {
            reader.readNext();
            if (!reader.isStartElement())
            {
                continue;
            }

            const QString elementName = reader.name().toString().trimmed().toCaseFolded();
            if (!rootFound && (elementName == QStringLiteral("tsresource") || elementName == QStringLiteral("resource")))
            {
                rootFound = true;
                const QXmlStreamAttributes attributes = reader.attributes();
                outMetadata->resourceId = decodeXmlEntities(attributes.value(QStringLiteral("id")).toString()).trimmed();
                outMetadata->resourcePath = decodeXmlEntities(
                    attributes.value(QStringLiteral("resourcePath")).toString()).trimmed();
                outMetadata->bucket = decodeXmlEntities(attributes.value(QStringLiteral("bucket")).toString()).trimmed();
                outMetadata->type = decodeXmlEntities(attributes.value(QStringLiteral("type")).toString()).trimmed();
                outMetadata->format = decodeXmlEntities(attributes.value(QStringLiteral("format")).toString()).trimmed();
                continue;
            }

            if (rootFound && elementName == QStringLiteral("asset"))
            {
                const QXmlStreamAttributes attributes = reader.attributes();
                outMetadata->assetPath = decodeXmlEntities(
                    attributes.value(QStringLiteral("path")).toString()).trimmed();
                continue;
            }

            if (rootFound && elementName == QStringLiteral("annotation"))
            {
                const QXmlStreamAttributes attributes = reader.attributes();
                outMetadata->annotationPath = decodeXmlEntities(
                    attributes.value(QStringLiteral("path")).toString()).trimmed();
            }
        }

        if (reader.hasError())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Invalid resource.xml: %1").arg(reader.errorString());
            }
            return false;
        }
        if (!rootFound)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("resource.xml is missing a <tsresource> root element.");
            }
            return false;
        }
        return true;
    }

    inline QString resourcePathForPackageDirectory(const QString& packageDirectoryPath)
    {
        const QString normalizedPackagePath = normalizePath(packageDirectoryPath);
        const QFileInfo packageInfo(normalizedPackagePath);
        if (!packageInfo.isDir())
        {
            return {};
        }

        const QDir packageDir(normalizedPackagePath);
        QDir resourcesDir = packageDir;
        if (!resourcesDir.cdUp())
        {
            return packageInfo.fileName();
        }
        return normalizePath(QStringLiteral("%1/%2").arg(resourcesDir.dirName(), packageInfo.fileName()));
    }

    inline bool loadResourcePackageMetadata(
        const QString& packageDirectoryPath,
        ResourcePackageMetadata* outMetadata,
        QString* errorMessage = nullptr)
    {
        if (outMetadata == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("outMetadata must not be null.");
            }
            return false;
        }

        *outMetadata = {};

        const QString normalizedPackagePath = normalizePath(packageDirectoryPath);
        const QFileInfo packageInfo(normalizedPackagePath);
        if (!packageInfo.isDir() || !isResourcePackageDirectoryName(packageInfo.fileName()))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Not a .tsresource directory: %1").arg(normalizedPackagePath);
            }
            return false;
        }

        const QString metadataPath = metadataFilePathForPackage(normalizedPackagePath);
        QFile metadataFile(metadataPath);
        if (!metadataFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to open resource metadata: %1").arg(metadataPath);
            }
            return false;
        }

        QString parseError;
        if (!parseResourcePackageMetadataXml(QString::fromUtf8(metadataFile.readAll()), outMetadata, &parseError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = parseError;
            }
            return false;
        }

        finalizeMetadata(outMetadata, normalizedPackagePath, resourcePathForPackageDirectory(normalizedPackagePath));
        return true;
    }

    inline QString resolveAssetPathFromMetadata(
        const QString& packageDirectoryPath,
        const ResourcePackageMetadata& metadata)
    {
        const QString assetPath = normalizePath(metadata.assetPath);
        if (assetPath.isEmpty())
        {
            return {};
        }
        return QDir(packageDirectoryPath).absoluteFilePath(assetPath);
    }

    inline QStringList resourceReferenceBasePathsForContext(
        const QString& noteDirectoryPath,
        const QString& noteHeaderPath,
        const QString& hubRootPath = QString())
    {
        QStringList candidates;
        if (!noteDirectoryPath.trimmed().isEmpty())
        {
            candidates.push_back(normalizePath(noteDirectoryPath));
        }
        if (!noteHeaderPath.trimmed().isEmpty())
        {
            candidates.push_back(normalizePath(QFileInfo(noteHeaderPath).absolutePath()));
        }
        if (!hubRootPath.trimmed().isEmpty())
        {
            candidates.push_back(normalizePath(hubRootPath));
            candidates.push_back(normalizePath(QFileInfo(hubRootPath).absolutePath()));
        }
        candidates.removeDuplicates();
        return candidates;
    }

    inline QStringList resourceReferenceBasePathsForResourcesFile(const QString& resourcesFilePath)
    {
        QStringList candidates;
        const QFileInfo info(resourcesFilePath.trimmed());
        if (!info.exists())
        {
            return candidates;
        }

        const QString contentsDirectory = normalizePath(info.absolutePath());
        if (!contentsDirectory.isEmpty())
        {
            candidates.push_back(contentsDirectory);
        }

        QDir hubDir(contentsDirectory);
        if (hubDir.cdUp())
        {
            candidates.push_back(normalizePath(hubDir.absolutePath()));
            candidates.push_back(normalizePath(QFileInfo(hubDir.absolutePath()).absolutePath()));
        }

        candidates.removeDuplicates();
        return candidates;
    }

    inline QString resolvePackageDirectoryFromReference(const QString& rawReferencePath, QStringList candidateBasePaths)
    {
        const QString referencePath = decodeXmlEntities(rawReferencePath).trimmed();
        if (referencePath.isEmpty())
        {
            return {};
        }

        const auto normalizePackageCandidate = [](const QString& candidatePath) -> QString
        {
            const QString normalizedCandidatePath = normalizePath(candidatePath);
            const QFileInfo info(normalizedCandidatePath);
            if (info.isDir() && isResourcePackageDirectoryName(info.fileName()))
            {
                return normalizedCandidatePath;
            }
            return {};
        };

        if (QFileInfo(referencePath).isAbsolute())
        {
            return normalizePackageCandidate(referencePath);
        }

        const QUrl resourceUrl(referencePath);
        if (resourceUrl.isValid() && !resourceUrl.scheme().isEmpty())
        {
            if (!resourceUrl.isLocalFile())
            {
                return {};
            }
            return normalizePackageCandidate(resourceUrl.toLocalFile());
        }

        candidateBasePaths.removeAll(QString());
        candidateBasePaths.removeDuplicates();

        for (const QString& basePath : std::as_const(candidateBasePaths))
        {
            const QString resolvedPath = normalizePackageCandidate(QDir(basePath).absoluteFilePath(referencePath));
            if (!resolvedPath.isEmpty())
            {
                return resolvedPath;
            }
        }

        return {};
    }

    inline QString resolveAssetLocationFromReference(const QString& rawReferencePath, QStringList candidateBasePaths)
    {
        const QString referencePath = decodeXmlEntities(rawReferencePath).trimmed();
        if (referencePath.isEmpty())
        {
            return {};
        }

        const auto resolveCandidate = [](const QString& candidatePath) -> QString
        {
            const QString normalizedCandidatePath = normalizePath(candidatePath);
            const QFileInfo info(normalizedCandidatePath);
            if (info.isFile())
            {
                return normalizedCandidatePath;
            }
            if (info.isDir() && isResourcePackageDirectoryName(info.fileName()))
            {
                ResourcePackageMetadata metadata;
                QString errorMessage;
                if (!loadResourcePackageMetadata(normalizedCandidatePath, &metadata, &errorMessage))
                {
                    ThinkingSpace::Debug::trace(
                        QStringLiteral("resources.package"),
                        QStringLiteral("resolveCandidate.metadataFailed"),
                        QStringLiteral("path=%1 reason=%2").arg(normalizedCandidatePath, errorMessage));
                    return {};
                }

                const QString assetPath = resolveAssetPathFromMetadata(normalizedCandidatePath, metadata);
                return QFileInfo(assetPath).isFile() ? normalizePath(assetPath) : QString();
            }
            return {};
        };

        if (QFileInfo(referencePath).isAbsolute())
        {
            return resolveCandidate(referencePath);
        }

        const QUrl resourceUrl(referencePath);
        if (resourceUrl.isValid() && !resourceUrl.scheme().isEmpty())
        {
            if (!resourceUrl.isLocalFile())
            {
                return resourceUrl.toString();
            }
            return resolveCandidate(resourceUrl.toLocalFile());
        }

        candidateBasePaths.removeAll(QString());
        candidateBasePaths.removeDuplicates();

        for (const QString& basePath : std::as_const(candidateBasePaths))
        {
            const QString candidatePath = QDir(basePath).absoluteFilePath(referencePath);
            const QString resolved = resolveCandidate(candidatePath);
            if (!resolved.isEmpty())
            {
                return resolved;
            }
        }

        return {};
    }

    inline QStringList resolveResourceRootDirectories(const QString& tshubPath)
    {
        QStringList directories;
        const QString normalizedWshubPath = normalizePath(tshubPath.trimmed());
        if (normalizedWshubPath.isEmpty())
        {
            return directories;
        }

        const QFileInfo hubInfo(normalizedWshubPath);
        if (!hubInfo.isDir())
        {
            return directories;
        }

        const QDir hubDir(normalizedWshubPath);
        const QString fixedPath = hubDir.filePath(QStringLiteral(".tsresources"));
        if (QFileInfo(fixedPath).isDir())
        {
            directories.push_back(normalizePath(fixedPath));
        }

        const QStringList wildcardDirectories = hubDir.entryList(
            QStringList{QStringLiteral("*.tsresources")},
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QString& directoryName : wildcardDirectories)
        {
            directories.push_back(normalizePath(hubDir.filePath(directoryName)));
        }

        directories.removeDuplicates();
        return directories;
    }

    inline QStringList listRelativeResourcePackagePaths(const QString& resourcesRootPath)
    {
        QStringList values;
        const QString normalizedResourcesRootPath = normalizePath(resourcesRootPath);
        const QFileInfo resourcesInfo(normalizedResourcesRootPath);
        if (!resourcesInfo.isDir())
        {
            return values;
        }

        const QDir resourcesDir(normalizedResourcesRootPath);
        const QFileInfoList packageDirectories = resourcesDir.entryInfoList(
            QStringList{QStringLiteral("*.tsresource")},
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);
        values.reserve(packageDirectories.size());

        for (const QFileInfo& packageInfo : packageDirectories)
        {
            values.push_back(normalizePath(
                QStringLiteral("%1/%2").arg(resourcesDir.dirName(), packageInfo.fileName())));
        }

        return values;
    }

    inline QStringList listRelativeResourcePackagePathsForHub(const QString& tshubPath)
    {
        QStringList values;
        const QStringList resourceRoots = resolveResourceRootDirectories(tshubPath);
        for (const QString& resourceRoot : resourceRoots)
        {
            const QStringList rootValues = listRelativeResourcePackagePaths(resourceRoot);
            for (const QString& rootValue : rootValues)
            {
                if (!values.contains(rootValue))
                {
                    values.push_back(rootValue);
                }
            }
        }
        return values;
    }

    inline int countResourcePackages(const QString& resourcesRootPath)
    {
        return listRelativeResourcePackagePaths(resourcesRootPath).size();
    }

    inline ResourcePackageMetadata buildFallbackMetadataFromResourcePath(
        const QString& resourcePath,
        const QString& resolvedAssetPath = QString())
    {
        ResourcePackageMetadata metadata;
        metadata.resourcePath = normalizePath(resourcePath);

        QString effectiveAssetPath = normalizePath(resolvedAssetPath);
        if (effectiveAssetPath.isEmpty())
        {
            effectiveAssetPath = normalizePath(resourcePath);
        }

        metadata.resourceId = QFileInfo(resourcePath).completeBaseName().trimmed();
        metadata.assetPath = QFileInfo(effectiveAssetPath).fileName();
        if (!isResourcePackageDirectoryName(QFileInfo(resourcePath).fileName()))
        {
            metadata.format = formatFromAssetFilePath(effectiveAssetPath);
        }
        metadata.type = inferTypeFromFormat(metadata.format);
        metadata.bucket = inferBucket(metadata.type, metadata.format);
        return metadata;
    }
} // namespace ThinkingSpace::Resources

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
