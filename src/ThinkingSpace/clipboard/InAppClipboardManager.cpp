#include "ThinkingSpace/clipboard/InAppClipboardManager.h"

#include "ThinkingSpace/clipboard/FiletypeCapture.h"
#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"
#include "ThinkingSpace/file/hub/ThinkingSpaceHubPathUtils.hpp"
#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcePackageSupport.hpp"
#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcesHierarchyParser.hpp"
#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcesHierarchyStore.hpp"

#include <QByteArray>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QIODevice>
#include <QMetaType>
#include <QMimeData>
#include <QPixmap>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSequentialIterable>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QVariantMap>

#include <utility>

namespace
{
    QString normalizedMimeFormat(QString value)
    {
        value = value.trimmed().toCaseFolded();
        const int parameterIndex = value.indexOf(QLatin1Char(';'));
        if (parameterIndex >= 0)
        {
            value = value.left(parameterIndex).trimmed();
        }
        return value;
    }

    bool mimeFormatLooksLikeSupportedResource(const QString& mimeFormat)
    {
        return !ThinkingSpace::Clipboard::FiletypeCapture::formatFromMimeType(mimeFormat).trimmed().isEmpty();
    }

    bool mimeFormatLooksLikeImagePayload(const QString& mimeFormat)
    {
        return ThinkingSpace::Clipboard::FiletypeCapture::mimeTypeLooksLikeImagePayload(mimeFormat);
    }

    QByteArray payloadForMimeFormat(const QMimeData* mimeData, const QString& mimeFormat)
    {
        if (mimeData == nullptr)
        {
            return {};
        }

        QByteArray payload = mimeData->data(mimeFormat);
        if (!payload.isEmpty())
        {
            return payload;
        }

        const QString normalized = normalizedMimeFormat(mimeFormat);
        if ((normalized == QStringLiteral("text/plain")
             || normalized == QStringLiteral("public.plain-text")
             || normalized == QStringLiteral("text/markdown")
             || normalized == QStringLiteral("text/csv"))
            && mimeData->hasText())
        {
            return mimeData->text().toUtf8();
        }

        if ((normalized == QStringLiteral("text/html")
             || normalized == QStringLiteral("public.html"))
            && mimeData->hasHtml())
        {
            return mimeData->html().toUtf8();
        }

        return {};
    }

    QString firstClipboardImageDataUrl(const QMimeData* mimeData)
    {
        if (mimeData == nullptr)
        {
            return {};
        }

        const auto extractFromText = [](const QString& text) -> QString
        {
            const QString trimmedText = text.trimmed();
            if (trimmedText.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive))
            {
                return trimmedText;
            }

            static const QRegularExpression quotedImageSrcPattern(
                QStringLiteral(R"(src\s*=\s*["'](data:image\/[^"']+)["'])"),
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match = quotedImageSrcPattern.match(text);
            if (match.hasMatch())
            {
                return match.captured(1).trimmed();
            }

            static const QRegularExpression bareDataUrlPattern(
                QStringLiteral(R"((data:image\/[^\s"'<>]+))"),
                QRegularExpression::CaseInsensitiveOption);
            match = bareDataUrlPattern.match(text);
            return match.hasMatch() ? match.captured(1).trimmed() : QString();
        };

        if (mimeData->hasHtml())
        {
            const QString htmlDataUrl = extractFromText(mimeData->html());
            if (!htmlDataUrl.isEmpty())
            {
                return htmlDataUrl;
            }
        }
        if (mimeData->hasText())
        {
            const QString textDataUrl = extractFromText(mimeData->text());
            if (!textDataUrl.isEmpty())
            {
                return textDataUrl;
            }
        }

        const QStringList formats = mimeData->formats();
        for (const QString& mimeFormat : formats)
        {
            if (!mimeFormat.startsWith(QStringLiteral("text/"), Qt::CaseInsensitive))
            {
                continue;
            }

            const QString payloadText = QString::fromUtf8(mimeData->data(mimeFormat));
            const QString dataUrl = extractFromText(payloadText);
            if (!dataUrl.isEmpty())
            {
                return dataUrl;
            }
        }
        return {};
    }

    QByteArray decodedClipboardImageDataUrlPayload(const QString& dataUrl)
    {
        const QString normalizedDataUrl = dataUrl.trimmed();
        if (!normalizedDataUrl.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive))
        {
            return {};
        }

        const int commaIndex = normalizedDataUrl.indexOf(QLatin1Char(','));
        if (commaIndex <= 0)
        {
            return {};
        }

        const QString header = normalizedDataUrl.left(commaIndex);
        QString payload = normalizedDataUrl.mid(commaIndex + 1);
        if (payload.isEmpty())
        {
            return {};
        }

        if (header.contains(QStringLiteral(";base64"), Qt::CaseInsensitive))
        {
            payload.remove(QRegularExpression(QStringLiteral("\\s+")));
            return QByteArray::fromBase64(payload.toUtf8());
        }

        return QByteArray::fromPercentEncoding(payload.toUtf8());
    }

    QImage imageFromVariant(const QVariant& imageVariant)
    {
        if (imageVariant.canConvert<QImage>())
        {
            return qvariant_cast<QImage>(imageVariant);
        }
        if (imageVariant.canConvert<QPixmap>())
        {
            return qvariant_cast<QPixmap>(imageVariant).toImage();
        }
        return {};
    }
} // namespace

InAppClipboardManager::InAppClipboardManager(QObject* parent)
    : QObject(parent)
{
    connect(&m_store, &InAppClipboardStore::resourceChanged, this, &InAppClipboardManager::resourceChanged);
}

InAppClipboardManager::~InAppClipboardManager() = default;

bool InAppClipboardManager::hasResource() const noexcept
{
    return m_store.hasResource();
}

QString InAppClipboardManager::resourceFileName() const
{
    return m_store.resourceFileName();
}

QString InAppClipboardManager::resourceFormat() const
{
    return m_store.resourceFormat();
}

QString InAppClipboardManager::resourceType() const
{
    return m_store.resourceType();
}

QString InAppClipboardManager::resourceBucket() const
{
    return m_store.resourceBucket();
}

QString InAppClipboardManager::resourceMimeType() const
{
    return m_store.resourceMimeType();
}

QVariantMap InAppClipboardManager::resourceEntry() const
{
    return m_store.resourceEntry();
}

const ThinkingSpace::Clipboard::ClipboardResourceImport& InAppClipboardManager::resourceImport() const noexcept
{
    return m_store.resourceImport();
}

ThinkingSpace::Clipboard::ClipboardResourceImport InAppClipboardManager::takeResourceImport()
{
    return m_store.takeResourceImport();
}

bool InAppClipboardManager::captureSystemClipboardResource()
{
    return captureResourceFromClipboard(QGuiApplication::clipboard());
}

bool InAppClipboardManager::captureResourceFromClipboard(const QClipboard* clipboard)
{
    if (clipboard == nullptr)
    {
        clear();
        return false;
    }

    if (captureResourceFromMimeData(clipboard->mimeData()))
    {
        return true;
    }

    const QImage clipboardImage = clipboard->image();
    if (!clipboardImage.isNull())
    {
        return setImageResource(clipboardImage);
    }

    const QPixmap clipboardPixmap = clipboard->pixmap();
    if (!clipboardPixmap.isNull())
    {
        return setImageResource(clipboardPixmap.toImage());
    }

    clear();
    return false;
}

bool InAppClipboardManager::captureResourceFromMimeData(const QMimeData* mimeData)
{
    if (mimeData == nullptr)
    {
        clear();
        return false;
    }

    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl& url : urls)
    {
        if (url.isValid() && url.isLocalFile())
        {
            if (setResourceLocalFile(url.toLocalFile()))
            {
                return true;
            }
        }
    }

    const QStringList formats = mimeData->formats();
    for (const QString& mimeFormat : formats)
    {
        if (!mimeFormatLooksLikeImagePayload(mimeFormat))
        {
            continue;
        }

        QImage image;
        if (image.loadFromData(mimeData->data(mimeFormat)) && !image.isNull())
        {
            return setImageResource(image);
        }
    }

    const QString imageDataUrl = firstClipboardImageDataUrl(mimeData);
    if (!imageDataUrl.isEmpty())
    {
        QImage image;
        const QByteArray imageBytes = decodedClipboardImageDataUrlPayload(imageDataUrl);
        if (!imageBytes.isEmpty() && image.loadFromData(imageBytes) && !image.isNull())
        {
            return setImageResource(image);
        }
    }

    if (mimeData->hasImage())
    {
        const QImage image = imageFromVariant(mimeData->imageData());
        if (!image.isNull())
        {
            return setImageResource(image);
        }
    }

    for (const QString& mimeFormat : formats)
    {
        if (!mimeFormatLooksLikeSupportedResource(mimeFormat))
        {
            continue;
        }

        const QByteArray payload = payloadForMimeFormat(mimeData, mimeFormat);
        QImage image;
        if (payload.isEmpty() || image.loadFromData(payload))
        {
            if (!image.isNull())
            {
                return setImageResource(image);
            }
        }
        if (!payload.isEmpty())
        {
            return setResourceImport(ThinkingSpace::Clipboard::resourceImportForBytes(
                payload,
                QString(),
                mimeFormat));
        }
    }

    clear();
    return false;
}

bool InAppClipboardManager::setResourceFileType(const QString& fileName, const QString& mimeType)
{
    ThinkingSpace::Clipboard::ClipboardResourceImport resourceImport =
        ThinkingSpace::Clipboard::resourceImportForFileType(fileName, mimeType);
    return setResourceImport(std::move(resourceImport));
}

bool InAppClipboardManager::setResourceLocalFile(const QString& localFilePath, const QString& mimeType)
{
    const QString trimmedLocalFilePath = localFilePath.trimmed();
    if (trimmedLocalFilePath.isEmpty() || !QFileInfo(trimmedLocalFilePath).isFile())
    {
        clear();
        return false;
    }

    ThinkingSpace::Clipboard::ClipboardResourceImport resourceImport =
        ThinkingSpace::Clipboard::resourceImportForLocalFile(trimmedLocalFilePath, mimeType);
    return setResourceImport(std::move(resourceImport));
}

bool InAppClipboardManager::setResourceBytes(
    const QByteArray& bytes,
    const QString& fileName,
    const QString& mimeType)
{
    if (bytes.isEmpty())
    {
        clear();
        return false;
    }

    ThinkingSpace::Clipboard::ClipboardResourceImport resourceImport =
        ThinkingSpace::Clipboard::resourceImportForBytes(bytes, fileName, mimeType);
    return setResourceImport(std::move(resourceImport));
}

bool InAppClipboardManager::setResourceText(
    const QString& text,
    const QString& fileName,
    const QString& mimeType)
{
    if (text.isEmpty())
    {
        clear();
        return false;
    }

    return setResourceBytes(text.toUtf8(), fileName, mimeType);
}

bool InAppClipboardManager::setImageResource(const QImage& image, const QString& mimeType)
{
    if (image.isNull())
    {
        clear();
        return false;
    }

    return setResourceImport(ThinkingSpace::Clipboard::resourceImportForImage(image, mimeType));
}

bool InAppClipboardManager::setResourceImport(ThinkingSpace::Clipboard::ClipboardResourceImport resourceImport)
{
    return m_store.setResourceImport(std::move(resourceImport));
}

void InAppClipboardManager::clear()
{
    m_store.clear();
}

namespace
{
    constexpr auto kScope = "clipboard.import";

    enum class ImportConflictPolicyValue
    {
        Abort = 0,
        Overwrite = 1,
        KeepBoth = 2
    };

    struct ExistingResourcePackageEntry final
    {
        QString assetFileName;
        ThinkingSpace::Resources::ResourcePackageMetadata metadata;
        QString packageDirectoryPath;
        QString resourcePath;
    };

    struct ImportConflictDescriptor final
    {
        QString existingAssetFileName;
        ThinkingSpace::Resources::ResourcePackageMetadata existingMetadata;
        QString packageDirectoryPath;
        QString resourcePath;
        QString sourceFileName;
        QString sourceFilePath;

        bool valid() const
        {
            return !existingAssetFileName.trimmed().isEmpty()
                && !packageDirectoryPath.trimmed().isEmpty()
                && !sourceFileName.trimmed().isEmpty();
        }
    };

    struct OverwrittenPackageBackup final
    {
        QString backupDirectoryPath;
        QString packageDirectoryPath;
    };

    ImportConflictPolicyValue normalizedImportConflictPolicy(const int conflictPolicy)
    {
        switch (conflictPolicy)
        {
        case InAppClipboardManager::ConflictPolicyOverwrite:
            return ImportConflictPolicyValue::Overwrite;
        case InAppClipboardManager::ConflictPolicyKeepBoth:
            return ImportConflictPolicyValue::KeepBoth;
        default:
            return ImportConflictPolicyValue::Abort;
        }
    }

    QVariantMap emptyImportConflictMap()
    {
        return QVariantMap {
            {QStringLiteral("conflict"), false}
        };
    }

    QVariantMap importConflictMap(const ImportConflictDescriptor& descriptor)
    {
        if (!descriptor.valid())
        {
            return emptyImportConflictMap();
        }

        QVariantMap result;
        result.insert(QStringLiteral("conflict"), true);
        result.insert(QStringLiteral("sourceFileName"), descriptor.sourceFileName);
        result.insert(QStringLiteral("sourceFilePath"), descriptor.sourceFilePath);
        result.insert(QStringLiteral("existingAssetFileName"), descriptor.existingAssetFileName);
        result.insert(QStringLiteral("existingPackageDirectoryPath"), descriptor.packageDirectoryPath);
        result.insert(QStringLiteral("existingResourcePath"), descriptor.resourcePath);
        result.insert(QStringLiteral("existingResourceId"), descriptor.existingMetadata.resourceId.trimmed());
        result.insert(QStringLiteral("existingAssetPath"), descriptor.existingMetadata.assetPath.trimmed());
        result.insert(QStringLiteral("existingType"), descriptor.existingMetadata.type.trimmed());
        result.insert(QStringLiteral("existingFormat"), descriptor.existingMetadata.format.trimmed());
        return result;
    }

    QString resolvePrimaryHubDirectory(
        const QString& hubPath,
        const QString& fixedDirectoryName,
        const QString& wildcardPattern,
        QString* errorMessage = nullptr)
    {
        const QString normalizedHubPath = ThinkingSpace::HubPath::normalizeAbsolutePath(hubPath);
        const QFileInfo hubInfo(normalizedHubPath);
        if (!hubInfo.exists() || !hubInfo.isDir()
            || !hubInfo.fileName().endsWith(QStringLiteral(".tshub"), Qt::CaseInsensitive))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Current hub path is not an unpacked .tshub directory: %1").arg(
                    normalizedHubPath);
            }
            return {};
        }

        const QDir hubDir(normalizedHubPath);
        const QString fixedPath = hubDir.filePath(fixedDirectoryName);
        if (QFileInfo(fixedPath).isDir())
        {
            return ThinkingSpace::Resources::normalizePath(fixedPath);
        }

        const QStringList candidates = hubDir.entryList(
            QStringList{wildcardPattern},
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);
        if (!candidates.isEmpty())
        {
            return ThinkingSpace::Resources::normalizePath(hubDir.filePath(candidates.constFirst()));
        }

        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Required hub directory is missing: %1 inside %2").arg(
                wildcardPattern,
                normalizedHubPath);
        }
        return {};
    }

    QString resolveContentsDirectory(const QString& hubPath, QString* errorMessage = nullptr)
    {
        return resolvePrimaryHubDirectory(
            hubPath,
            QStringLiteral(".tscontents"),
            QStringLiteral("*.tscontents"),
            errorMessage);
    }

    QString resourceRootNameFromResourcePath(const QString& resourcePath)
    {
        const QString normalizedPath = ThinkingSpace::Resources::normalizePath(resourcePath.trimmed());
        const int separatorIndex = normalizedPath.indexOf(QLatin1Char('/'));
        if (separatorIndex <= 0)
        {
            return {};
        }

        const QString rootName = normalizedPath.left(separatorIndex).trimmed();
        if (!rootName.endsWith(QStringLiteral(".tsresources"), Qt::CaseInsensitive))
        {
            return {};
        }
        return rootName;
    }

    QString resolveResourcesDirectory(
        const QString& hubPath,
        const QStringList& existingResourcePaths = {},
        QString* errorMessage = nullptr)
    {
        const QStringList candidates = ThinkingSpace::Resources::resolveResourceRootDirectories(hubPath);
        if (candidates.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Required hub directory is missing: *.tsresources inside %1").arg(
                    ThinkingSpace::HubPath::normalizeAbsolutePath(hubPath));
            }
            return {};
        }

        QSet<QString> referencedRootNames;
        for (const QString& resourcePath : existingResourcePaths)
        {
            const QString rootName = resourceRootNameFromResourcePath(resourcePath);
            if (rootName.isEmpty())
            {
                continue;
            }
            referencedRootNames.insert(rootName.toCaseFolded());
        }

        if (!referencedRootNames.isEmpty())
        {
            for (const QString& candidatePath : candidates)
            {
                const QString candidateDirectoryName = QFileInfo(candidatePath).fileName().trimmed().toCaseFolded();
                if (referencedRootNames.contains(candidateDirectoryName))
                {
                    return ThinkingSpace::Resources::normalizePath(candidatePath);
                }
            }
        }

        return candidates.constFirst();
    }

    QString sanitizeResourceId(QString value)
    {
        value = QFileInfo(value.trimmed()).completeBaseName().trimmed().toCaseFolded();
        value.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("-"));
        value.replace(QRegularExpression(QStringLiteral("-{2,}")), QStringLiteral("-"));
        value.remove(QRegularExpression(QStringLiteral("^-+")));
        value.remove(QRegularExpression(QStringLiteral("-+$")));
        if (value.isEmpty())
        {
            value = QStringLiteral("resource");
        }
        return value;
    }

    QSet<QString> existingResourceIdsForPackages(const QString& resourcesDirectoryPath)
    {
        const QFileInfoList packageDirectories = QDir(resourcesDirectoryPath).entryInfoList(
            QStringList{QStringLiteral("*.tsresource")},
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);

        QSet<QString> existingIds;
        existingIds.reserve(packageDirectories.size());
        for (const QFileInfo& packageDirectory : packageDirectories)
        {
            existingIds.insert(
                ThinkingSpace::Resources::resourceIdFromPackageName(packageDirectory.fileName()).toCaseFolded());
        }
        return existingIds;
    }

    QString uniqueResourceIdForFile(const QString& resourcesDirectoryPath, const QString& sourceFilePath)
    {
        const QSet<QString> existingIds = existingResourceIdsForPackages(resourcesDirectoryPath);
        const QString baseId = sanitizeResourceId(sourceFilePath);
        QString candidateId = baseId;
        int suffix = 2;
        while (existingIds.contains(candidateId.toCaseFolded()))
        {
            candidateId = QStringLiteral("%1-%2").arg(baseId).arg(suffix);
            ++suffix;
        }

        return candidateId;
    }

    QString randomClipboardResourceId()
    {
        constexpr int kRandomClipboardResourceIdLength = 32;
        static const QString kAlphabet =
            QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

        QString value;
        value.reserve(kRandomClipboardResourceIdLength);
        for (int i = 0; i < kRandomClipboardResourceIdLength; ++i)
        {
            value.append(kAlphabet.at(QRandomGenerator::global()->bounded(kAlphabet.size())));
        }
        return value;
    }

    QString uniqueRandomClipboardResourceId(const QString& resourcesDirectoryPath)
    {
        const QSet<QString> existingIds = existingResourceIdsForPackages(resourcesDirectoryPath);
        QString candidateId;
        do
        {
            candidateId = randomClipboardResourceId();
        } while (existingIds.contains(candidateId.toCaseFolded()));
        return candidateId;
    }

    bool isDefaultMaterializedClipboardResourceFile(const QFileInfo& sourceFileInfo)
    {
        return sourceFileInfo.completeBaseName().trimmed() == QStringLiteral("clipboard-resource");
    }

    bool shouldRandomizeDefaultClipboardResourceFile(
        const QString& sourceFilePath,
        const bool randomizeDefaultClipboardResourceNames)
    {
        return randomizeDefaultClipboardResourceNames
            && isDefaultMaterializedClipboardResourceFile(QFileInfo(sourceFilePath));
    }

    QStringList conflictCheckedSourceFiles(
        const QStringList& sourceFiles,
        const bool randomizeDefaultClipboardResourceNames)
    {
        QStringList checkedFiles;
        checkedFiles.reserve(sourceFiles.size());
        for (const QString& sourceFilePath : sourceFiles)
        {
            if (shouldRandomizeDefaultClipboardResourceFile(
                sourceFilePath,
                randomizeDefaultClipboardResourceNames))
            {
                continue;
            }
            checkedFiles.push_back(sourceFilePath);
        }
        return checkedFiles;
    }

    QString clipboardAssetFileNameForResourceId(
        const QFileInfo& sourceFileInfo,
        const QString& resourceId)
    {
        QString suffix = sourceFileInfo.suffix().trimmed();
        if (suffix.isEmpty())
        {
            suffix = QStringLiteral("bin");
        }
        return QStringLiteral("%1.%2").arg(resourceId, suffix);
    }

    bool loadExistingResourcePackageEntries(
        const QString& resourcesDirectoryPath,
        QList<ExistingResourcePackageEntry>* outEntries,
        QString* errorMessage = nullptr)
    {
        if (outEntries == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("outEntries must not be null.");
            }
            return false;
        }

        outEntries->clear();
        const QFileInfoList packageDirectories = QDir(resourcesDirectoryPath).entryInfoList(
            QStringList{QStringLiteral("*.tsresource")},
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);

        for (const QFileInfo& packageDirectoryInfo : packageDirectories)
        {
            ExistingResourcePackageEntry entry;
            entry.packageDirectoryPath = ThinkingSpace::Resources::normalizePath(packageDirectoryInfo.absoluteFilePath());
            entry.resourcePath = ThinkingSpace::Resources::resourcePathForPackageDirectory(entry.packageDirectoryPath);
            QString metadataError;
            if (!ThinkingSpace::Resources::loadResourcePackageMetadata(
                entry.packageDirectoryPath,
                &entry.metadata,
                &metadataError))
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = metadataError;
                }
                return false;
            }

            if (entry.metadata.resourcePath.trimmed().isEmpty())
            {
                entry.metadata.resourcePath = entry.resourcePath;
            }
            entry.assetFileName = QFileInfo(entry.metadata.assetPath.trimmed()).fileName().trimmed();
            if (entry.assetFileName.isEmpty())
            {
                entry.assetFileName = QFileInfo(entry.packageDirectoryPath).fileName().trimmed();
            }
            outEntries->push_back(entry);
        }

        return true;
    }

    bool findFirstImportConflict(
        const QStringList& sourceFileNames,
        const QStringList& sourceFilePaths,
        const QString& resourcesDirectoryPath,
        ImportConflictDescriptor* outDescriptor,
        QString* errorMessage = nullptr)
    {
        if (outDescriptor != nullptr)
        {
            *outDescriptor = {};
        }

        QList<ExistingResourcePackageEntry> existingEntries;
        if (!loadExistingResourcePackageEntries(resourcesDirectoryPath, &existingEntries, errorMessage))
        {
            return false;
        }

        QHash<QString, ExistingResourcePackageEntry> entriesByFileName;
        for (const ExistingResourcePackageEntry& entry : std::as_const(existingEntries))
        {
            const QString lookupKey = entry.assetFileName.trimmed().toCaseFolded();
            if (lookupKey.isEmpty() || entriesByFileName.contains(lookupKey))
            {
                continue;
            }
            entriesByFileName.insert(lookupKey, entry);
        }

        const int entryCount = std::min(sourceFileNames.size(), sourceFilePaths.size());
        for (int index = 0; index < entryCount; ++index)
        {
            const QString sourceFileName = sourceFileNames.at(index).trimmed();
            const QString lookupKey = sourceFileName.toCaseFolded();
            if (lookupKey.isEmpty() || !entriesByFileName.contains(lookupKey))
            {
                continue;
            }

            const ExistingResourcePackageEntry existingEntry = entriesByFileName.value(lookupKey);
            if (outDescriptor != nullptr)
            {
                outDescriptor->existingAssetFileName = existingEntry.assetFileName;
                outDescriptor->existingMetadata = existingEntry.metadata;
                outDescriptor->packageDirectoryPath = existingEntry.packageDirectoryPath;
                outDescriptor->resourcePath = existingEntry.resourcePath;
                outDescriptor->sourceFileName = sourceFileName;
                outDescriptor->sourceFilePath = sourceFilePaths.at(index).trimmed();
            }
            return true;
        }

        return true;
    }

    bool findFirstImportConflict(
        const QStringList& sourceFiles,
        const QString& resourcesDirectoryPath,
        ImportConflictDescriptor* outDescriptor,
        QString* errorMessage = nullptr)
    {
        QStringList sourceFileNames;
        QStringList normalizedSourcePaths;
        sourceFileNames.reserve(sourceFiles.size());
        normalizedSourcePaths.reserve(sourceFiles.size());
        for (const QString& sourceFilePath : sourceFiles)
        {
            const QString sourceFileName = QFileInfo(sourceFilePath).fileName().trimmed();
            sourceFileNames.push_back(sourceFileName);
            normalizedSourcePaths.push_back(ThinkingSpace::HubPath::normalizeAbsolutePath(sourceFilePath));
        }
        return findFirstImportConflict(
            sourceFileNames,
            normalizedSourcePaths,
            resourcesDirectoryPath,
            outDescriptor,
            errorMessage);
    }

    bool writeUtf8FileAtomically(const QString& filePath, const QString& text, QString* errorMessage = nullptr)
    {
        const QString directoryPath = QFileInfo(filePath).absolutePath();
        if (!directoryPath.isEmpty() && !QDir().mkpath(directoryPath))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to create directory for file write: %1").arg(directoryPath);
            }
            return false;
        }

        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to open file for write: %1").arg(filePath);
            }
            return false;
        }

        if (file.write(text.toUtf8()) < 0)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to write file: %1").arg(filePath);
            }
            file.cancelWriting();
            return false;
        }

        if (!file.commit())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to commit file write: %1").arg(filePath);
            }
            return false;
        }

        return true;
    }

    QString duplicateImportResolutionRequiredMessage(const ImportConflictDescriptor& descriptor)
    {
        const QString fileName = descriptor.sourceFileName.trimmed().isEmpty()
            ? descriptor.existingAssetFileName.trimmed()
            : descriptor.sourceFileName.trimmed();
        return QStringLiteral(
                   "A resource named \"%1\" already exists. Choose overwrite, keep both, or cancel from the duplicate import alert.")
            .arg(fileName);
    }

    bool readUtf8FileText(const QString& filePath, QString* outText, QString* errorMessage = nullptr)
    {
        if (outText == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("outText must not be null.");
            }
            return false;
        }

        outText->clear();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to open file for read: %1").arg(filePath);
            }
            return false;
        }

        *outText = QString::fromUtf8(file.readAll());
        return true;
    }

    void appendLocalFilePath(const QString& filePath, QStringList* localFiles)
    {
        if (localFiles == nullptr)
        {
            return;
        }

        const QString absolutePath = ThinkingSpace::HubPath::normalizeAbsolutePath(filePath);
        const QFileInfo fileInfo(absolutePath);
        if (!fileInfo.exists() || !fileInfo.isFile())
        {
            return;
        }

        if (!localFiles->contains(absolutePath))
        {
            localFiles->push_back(absolutePath);
        }
    }

    void appendLocalFilesFromVariant(const QVariant& entry, QStringList* localFiles)
    {
        if (localFiles == nullptr || !entry.isValid())
        {
            return;
        }

        if (entry.metaType().id() == QMetaType::QVariantList)
        {
            const QVariantList nestedValues = entry.toList();
            for (const QVariant& nestedValue : nestedValues)
            {
                appendLocalFilesFromVariant(nestedValue, localFiles);
            }
            return;
        }

        if (entry.metaType().id() == QMetaType::QStringList)
        {
            const QStringList nestedValues = entry.toStringList();
            for (const QString& nestedValue : nestedValues)
            {
                appendLocalFilesFromVariant(nestedValue, localFiles);
            }
            return;
        }

        if (entry.canConvert<QSequentialIterable>())
        {
            const QSequentialIterable iterable = entry.value<QSequentialIterable>();
            bool iteratedAny = false;
            for (auto it = iterable.begin(); it != iterable.end(); ++it)
            {
                iteratedAny = true;
                appendLocalFilesFromVariant(*it, localFiles);
            }
            if (iteratedAny)
            {
                return;
            }
        }

        QUrl url = entry.toUrl();
        if (!url.isValid() || url.isEmpty())
        {
            const QString rawText = entry.toString().trimmed();
            if (rawText.isEmpty())
            {
                return;
            }
            url = QUrl::fromUserInput(rawText);
        }

        if (!url.isValid() || !url.isLocalFile())
        {
            return;
        }

        appendLocalFilePath(url.toLocalFile(), localFiles);
    }

    QStringList extractDroppedLocalFiles(const QVariantList& urls)
    {
        QStringList localFiles;
        for (const QVariant& entry : urls)
        {
            appendLocalFilesFromVariant(entry, &localFiles);
        }
        return localFiles;
    }

    bool loadExistingResourcePaths(const QString& resourcesFilePath, QStringList* outPaths, QString* errorMessage = nullptr)
    {
        if (outPaths == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("outPaths must not be null.");
            }
            return false;
        }

        outPaths->clear();
        if (!QFileInfo(resourcesFilePath).isFile())
        {
            return true;
        }

        QFile file(resourcesFilePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to open Resources.tsresources: %1").arg(resourcesFilePath);
            }
            return false;
        }

        ThinkingSpaceResourcesHierarchyParser parser;
        ThinkingSpaceResourcesHierarchyStore store;
        QString parseError;
        const QString rawText = QString::fromUtf8(file.readAll());
        if (!parser.parse(rawText, &store, &parseError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = parseError;
            }
            return false;
        }

        *outPaths = store.resourcePaths();
        return true;
    }

    bool importSingleFile(
        const QString& sourceFilePath,
        const QString& resourcesDirectoryPath,
        const bool randomizeDefaultClipboardResourceNames,
        QString* outResourcePath,
        QString* outCreatedPackagePath,
        ThinkingSpace::Resources::ResourcePackageMetadata* outMetadata,
        QString* errorMessage = nullptr)
    {
        if (outResourcePath == nullptr || outCreatedPackagePath == nullptr || outMetadata == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Output pointers must not be null.");
            }
            return false;
        }

        *outResourcePath = QString();
        *outCreatedPackagePath = QString();
        *outMetadata = {};

        const QFileInfo sourceFileInfo(sourceFilePath);
        if (!sourceFileInfo.exists() || !sourceFileInfo.isFile())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Dropped file does not exist: %1").arg(sourceFilePath);
            }
            return false;
        }

        const bool defaultClipboardResourceFile =
            randomizeDefaultClipboardResourceNames
            && isDefaultMaterializedClipboardResourceFile(sourceFileInfo);
        const QString resourceId = defaultClipboardResourceFile
            ? uniqueRandomClipboardResourceId(resourcesDirectoryPath)
            : uniqueResourceIdForFile(resourcesDirectoryPath, sourceFilePath);
        const QString packageDirectoryPath = QDir(resourcesDirectoryPath).filePath(
            resourceId + ThinkingSpace::Resources::packageDirectorySuffix());
        const QString destinationAssetFileName = defaultClipboardResourceFile
            ? clipboardAssetFileNameForResourceId(sourceFileInfo, resourceId)
            : sourceFileInfo.fileName();
        const QString destinationAssetPath = QDir(packageDirectoryPath).filePath(destinationAssetFileName);
        const QString resourcePath = ThinkingSpace::Resources::normalizePath(
            QStringLiteral("%1/%2")
                .arg(QFileInfo(resourcesDirectoryPath).fileName(), QFileInfo(packageDirectoryPath).fileName()));

        if (!QDir().mkpath(packageDirectoryPath))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to create resource package directory: %1").arg(
                    packageDirectoryPath);
            }
            return false;
        }

        if (!QFile::copy(sourceFilePath, destinationAssetPath))
        {
            QDir(packageDirectoryPath).removeRecursively();
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to copy dropped file into resource package: %1").arg(
                    sourceFilePath);
            }
            return false;
        }

        const ThinkingSpace::Resources::ResourcePackageMetadata metadata =
            ThinkingSpace::Resources::buildMetadataForAssetFile(
                destinationAssetFileName,
                resourceId,
                resourcePath);

        QString writeError;
        if (!ThinkingSpace::Resources::writeResourcePackageAnnotationBitmap(
            packageDirectoryPath,
            sourceFilePath,
            &writeError))
        {
            QDir(packageDirectoryPath).removeRecursively();
            if (errorMessage != nullptr)
            {
                *errorMessage = writeError;
            }
            return false;
        }

        if (!writeUtf8FileAtomically(
            QDir(packageDirectoryPath).filePath(ThinkingSpace::Resources::metadataFileName()),
            ThinkingSpace::Resources::createResourcePackageMetadataXml(metadata),
            &writeError))
        {
            QDir(packageDirectoryPath).removeRecursively();
            if (errorMessage != nullptr)
            {
                *errorMessage = writeError;
            }
            return false;
        }

        *outResourcePath = resourcePath;
        *outCreatedPackagePath = packageDirectoryPath;
        *outMetadata = metadata;
        return true;
    }

    bool overwriteSingleFile(
        const QString& sourceFilePath,
        const ImportConflictDescriptor& conflictDescriptor,
        const QString& resourcesDirectoryPath,
        QString* outResourcePath,
        QString* outCreatedPackagePath,
        ThinkingSpace::Resources::ResourcePackageMetadata* outMetadata,
        QString* outBackupDirectoryPath,
        QString* errorMessage = nullptr)
    {
        if (outResourcePath == nullptr
            || outCreatedPackagePath == nullptr
            || outMetadata == nullptr
            || outBackupDirectoryPath == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Output pointers must not be null.");
            }
            return false;
        }

        *outResourcePath = QString();
        *outCreatedPackagePath = QString();
        *outMetadata = {};
        *outBackupDirectoryPath = QString();

        const QFileInfo sourceFileInfo(sourceFilePath);
        if (!sourceFileInfo.exists() || !sourceFileInfo.isFile())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Dropped file does not exist: %1").arg(sourceFilePath);
            }
            return false;
        }

        const QString packageDirectoryPath =
            ThinkingSpace::Resources::normalizePath(conflictDescriptor.packageDirectoryPath);
        if (!QFileInfo(packageDirectoryPath).isDir())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Existing resource package is missing: %1").arg(packageDirectoryPath);
            }
            return false;
        }

        const QString backupDirectoryPath = QDir(resourcesDirectoryPath).filePath(
            QStringLiteral(".import-backup-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QDir().rename(packageDirectoryPath, backupDirectoryPath))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to stage the existing resource package for overwrite: %1").arg(
                    packageDirectoryPath);
            }
            return false;
        }

        const auto restoreBackupAndFail = [&](const QString& failureText)
        {
            if (QFileInfo(packageDirectoryPath).exists())
            {
                QDir(packageDirectoryPath).removeRecursively();
            }
            QDir().rename(backupDirectoryPath, packageDirectoryPath);
            if (errorMessage != nullptr)
            {
                *errorMessage = failureText;
            }
            return false;
        };

        if (!QDir().mkpath(packageDirectoryPath))
        {
            return restoreBackupAndFail(
                QStringLiteral("Failed to recreate overwritten resource package directory: %1").arg(
                    packageDirectoryPath));
        }

        const QString destinationAssetPath = QDir(packageDirectoryPath).filePath(sourceFileInfo.fileName());
        if (!QFile::copy(sourceFilePath, destinationAssetPath))
        {
            return restoreBackupAndFail(
                QStringLiteral("Failed to copy dropped file into overwritten resource package: %1").arg(
                    sourceFilePath));
        }

        ThinkingSpace::Resources::ResourcePackageMetadata metadata =
            ThinkingSpace::Resources::buildMetadataForAssetFile(
                sourceFileInfo.fileName(),
                conflictDescriptor.existingMetadata.resourceId.trimmed(),
                conflictDescriptor.resourcePath.trimmed());
        if (metadata.resourcePath.trimmed().isEmpty())
        {
            metadata.resourcePath = conflictDescriptor.resourcePath.trimmed();
        }

        QString writeError;
        if (!ThinkingSpace::Resources::writeResourcePackageAnnotationBitmap(
            packageDirectoryPath,
            sourceFilePath,
            &writeError))
        {
            return restoreBackupAndFail(writeError);
        }

        if (!writeUtf8FileAtomically(
            QDir(packageDirectoryPath).filePath(ThinkingSpace::Resources::metadataFileName()),
            ThinkingSpace::Resources::createResourcePackageMetadataXml(metadata),
            &writeError))
        {
            return restoreBackupAndFail(writeError);
        }

        *outResourcePath = metadata.resourcePath.trimmed().isEmpty()
            ? conflictDescriptor.resourcePath.trimmed()
            : metadata.resourcePath.trimmed();
        *outCreatedPackagePath = packageDirectoryPath;
        *outMetadata = metadata;
        *outBackupDirectoryPath = ThinkingSpace::Resources::normalizePath(backupDirectoryPath);
        return true;
    }

    QVariantMap importedEntryFromMetadata(const ThinkingSpace::Resources::ResourcePackageMetadata& metadata)
    {
        QVariantMap entry;
        entry.insert(QStringLiteral("resourceId"), metadata.resourceId.trimmed());
        entry.insert(QStringLiteral("resourcePath"), ThinkingSpace::Resources::normalizePath(metadata.resourcePath));
        entry.insert(QStringLiteral("assetPath"), ThinkingSpace::Resources::normalizePath(metadata.assetPath));
        entry.insert(QStringLiteral("annotationPath"), ThinkingSpace::Resources::normalizePath(metadata.annotationPath));
        entry.insert(QStringLiteral("bucket"), metadata.bucket.trimmed());
        entry.insert(QStringLiteral("type"), metadata.type.trimmed().toCaseFolded());
        entry.insert(QStringLiteral("format"), ThinkingSpace::Resources::normalizeFormat(metadata.format).toCaseFolded());
        return entry;
    }

    QString imageSaveFormatForResourceFormat(const QString& format)
    {
        const QString normalizedFormat = ThinkingSpace::Resources::normalizeFormat(format).toCaseFolded();
        if (normalizedFormat == QStringLiteral(".jpg") || normalizedFormat == QStringLiteral(".jpeg"))
        {
            return QStringLiteral("JPG");
        }
        if (normalizedFormat == QStringLiteral(".tif") || normalizedFormat == QStringLiteral(".tiff"))
        {
            return QStringLiteral("TIFF");
        }
        if (normalizedFormat == QStringLiteral(".bmp"))
        {
            return QStringLiteral("BMP");
        }
        if (normalizedFormat == QStringLiteral(".webp"))
        {
            return QStringLiteral("WEBP");
        }
        return QStringLiteral("PNG");
    }

    bool materializeClipboardResourceImport(
        const ThinkingSpace::Clipboard::ClipboardResourceImport& resourceImport,
        QTemporaryDir* temporaryDirectory,
        QString* outLocalFilePath,
        QString* errorMessage = nullptr)
    {
        if (outLocalFilePath == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("outLocalFilePath must not be null.");
            }
            return false;
        }

        *outLocalFilePath = QString();
        if (resourceImport.hasLocalFile())
        {
            const QString localFilePath = ThinkingSpace::HubPath::normalizeAbsolutePath(resourceImport.localFilePath);
            if (!QFileInfo(localFilePath).isFile())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Clipboard resource file does not exist: %1").arg(localFilePath);
                }
                return false;
            }
            *outLocalFilePath = localFilePath;
            return true;
        }

        if (!resourceImport.hasMemoryPayload())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Clipboard does not contain importable resource content.");
            }
            return false;
        }

        if (temporaryDirectory == nullptr || !temporaryDirectory->isValid())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to create temporary clipboard resource storage.");
            }
            return false;
        }

        const QString fileName = resourceImport.fileName.trimmed().isEmpty()
            ? ThinkingSpace::Clipboard::FiletypeCapture::defaultResourceFileName(resourceImport.format)
            : QFileInfo(resourceImport.fileName).fileName();
        const QString localFilePath = QDir(temporaryDirectory->path()).filePath(fileName);
        if (!resourceImport.payloadBytes.isEmpty())
        {
            QFile file(localFilePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || file.write(resourceImport.payloadBytes) != resourceImport.payloadBytes.size())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Failed to write clipboard resource payload: %1").arg(localFilePath);
                }
                return false;
            }
            *outLocalFilePath = localFilePath;
            return true;
        }

        if (!resourceImport.image.save(localFilePath, qPrintable(imageSaveFormatForResourceFormat(resourceImport.format))))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to write clipboard image resource: %1").arg(localFilePath);
            }
            return false;
        }

        *outLocalFilePath = localFilePath;
        return true;
    }

}

QString InAppClipboardManager::currentHubPath() const
{
    return m_currentHubPath;
}

void InAppClipboardManager::setCurrentHubPath(QString hubPath)
{
    hubPath = hubPath.trimmed().isEmpty()
                  ? QString()
                  : ThinkingSpace::HubPath::normalizeAbsolutePath(hubPath);
    if (m_currentHubPath == hubPath)
    {
        return;
    }

    m_currentHubPath = std::move(hubPath);
    ThinkingSpace::Debug::traceSelf(
        this,
        QString::fromLatin1(kScope),
        QStringLiteral("setCurrentHubPath"),
        QStringLiteral("value=%1").arg(m_currentHubPath));
    emit currentHubPathChanged();
}

bool InAppClipboardManager::busy() const noexcept
{
    return m_busy;
}

QString InAppClipboardManager::lastError() const
{
    return m_lastError;
}

void InAppClipboardManager::setReloadResourcesCallback(std::function<bool(const QString&, QString*)> callback)
{
    m_reloadResourcesCallback = std::move(callback);
}

bool InAppClipboardManager::canImportUrls(const QVariantList& urls) const
{
    if (m_busy || m_currentHubPath.trimmed().isEmpty())
    {
        return false;
    }

    return !extractDroppedLocalFiles(urls).isEmpty();
}

bool InAppClipboardManager::canImportDroppedUrls(const QVariantList& urls) const
{
    return canImportUrls(urls);
}

QVariantMap InAppClipboardManager::inspectImportConflictForUrls(const QVariantList& urls) const
{
    if (m_busy || m_currentHubPath.trimmed().isEmpty())
    {
        return emptyImportConflictMap();
    }

    const QStringList sourceFiles = extractDroppedLocalFiles(urls);
    if (sourceFiles.isEmpty())
    {
        return emptyImportConflictMap();
    }

    QString resolveError;
    QStringList existingResourcePaths;
    const QString contentsDirectoryPath = resolveContentsDirectory(m_currentHubPath, &resolveError);
    if (contentsDirectoryPath.isEmpty())
    {
        return emptyImportConflictMap();
    }

    const QString resourcesFilePath = QDir(contentsDirectoryPath).filePath(QStringLiteral("Resources.tsresources"));
    if (!loadExistingResourcePaths(resourcesFilePath, &existingResourcePaths, &resolveError))
    {
        return emptyImportConflictMap();
    }

    const QString resourcesDirectoryPath =
        resolveResourcesDirectory(m_currentHubPath, existingResourcePaths, &resolveError);
    if (resourcesDirectoryPath.isEmpty())
    {
        return emptyImportConflictMap();
    }

    ImportConflictDescriptor descriptor;
    if (!findFirstImportConflict(sourceFiles, resourcesDirectoryPath, &descriptor, &resolveError))
    {
        return emptyImportConflictMap();
    }
    return importConflictMap(descriptor);
}

bool InAppClipboardManager::importUrls(const QVariantList& urls)
{
    return importUrlsInternal(urls, nullptr, true, ConflictPolicyAbort, false);
}

bool InAppClipboardManager::importUrlsWithConflictPolicy(const QVariantList& urls, const int conflictPolicy)
{
    return importUrlsInternal(urls, nullptr, true, conflictPolicy, false);
}

QVariantList InAppClipboardManager::importUrlsForEditor(const QVariantList& urls)
{
    QVariantList importedEntries;
    if (!importUrlsInternal(urls, &importedEntries, false, ConflictPolicyAbort, false))
    {
        return {};
    }
    return importedEntries;
}

QVariantList InAppClipboardManager::importUrlsForEditorWithConflictPolicy(
    const QVariantList& urls,
    const int conflictPolicy)
{
    QVariantList importedEntries;
    if (!importUrlsInternal(urls, &importedEntries, false, conflictPolicy, false))
    {
        return {};
    }
    return importedEntries;
}

bool InAppClipboardManager::refreshClipboardResourceAvailabilitySnapshot()
{
    const bool captured = captureSystemClipboardResource();
    emit resourceChanged();
    return captured;
}

QVariantList InAppClipboardManager::importClipboardResourceForEditor()
{
    QVariantList importedEntries;
    if (!importClipboardResourceInternal(&importedEntries, false, ConflictPolicyAbort))
    {
        return {};
    }
    return importedEntries;
}

QVariantList InAppClipboardManager::importClipboardResourceForEditorWithConflictPolicy(const int conflictPolicy)
{
    QVariantList importedEntries;
    if (!importClipboardResourceInternal(&importedEntries, false, conflictPolicy))
    {
        return {};
    }
    return importedEntries;
}

bool InAppClipboardManager::importClipboardResourceInternal(
    QVariantList* importedEntries,
    const bool reloadRuntime,
    const int conflictPolicy)
{
    if (!hasResource() && !captureSystemClipboardResource())
    {
        const QString errorMessage = QStringLiteral("Clipboard does not contain an importable resource.");
        setLastError(errorMessage);
        emit operationFailed(errorMessage);
        emit resourceChanged();
        return false;
    }

    QTemporaryDir temporaryDirectory;
    QString localFilePath;
    QString materializeError;
    if (!materializeClipboardResourceImport(
        resourceImport(),
        &temporaryDirectory,
        &localFilePath,
        &materializeError))
    {
        setLastError(materializeError);
        emit operationFailed(materializeError);
        emit resourceChanged();
        return false;
    }

    const bool imported = importUrlsInternal(
        QVariantList{QUrl::fromLocalFile(localFilePath)},
        importedEntries,
        reloadRuntime,
        conflictPolicy,
        true);
    if (imported)
    {
        clear();
    }
    return imported;
}

bool InAppClipboardManager::importUrlsInternal(
    const QVariantList& urls,
    QVariantList* importedEntries,
    const bool reloadRuntime,
    const int conflictPolicy,
    const bool randomizeDefaultClipboardResourceNames)
{
    ThinkingSpace::Debug::traceSelf(
        this,
        QString::fromLatin1(kScope),
        importedEntries == nullptr
            ? QStringLiteral("importUrls.begin")
            : QStringLiteral("importUrlsForEditor.begin"),
        QStringLiteral("urlCount=%1 hubPath=%2").arg(urls.size()).arg(m_currentHubPath));

    if (m_busy)
    {
        const QString errorMessage = QStringLiteral("Resource import is already running.");
        setLastError(errorMessage);
        emit operationFailed(errorMessage);
        return false;
    }

    if (m_currentHubPath.trimmed().isEmpty())
    {
        const QString errorMessage = QStringLiteral("Current hub path is empty.");
        setLastError(errorMessage);
        emit operationFailed(errorMessage);
        return false;
    }

    const QStringList sourceFiles = extractDroppedLocalFiles(urls);
    if (sourceFiles.isEmpty())
    {
        const QString errorMessage = QStringLiteral("Select at least one local file to import as a resource.");
        setLastError(errorMessage);
        emit operationFailed(errorMessage);
        return false;
    }

    QString resolveError;
    QString contentsDirectoryPath = resolveContentsDirectory(m_currentHubPath, &resolveError);
    if (contentsDirectoryPath.isEmpty())
    {
        setLastError(resolveError);
        emit operationFailed(resolveError);
        return false;
    }

    const QString resourcesFilePath = QDir(contentsDirectoryPath).filePath(QStringLiteral("Resources.tsresources"));
    const bool hadResourcesFile = QFileInfo(resourcesFilePath).isFile();
    QString previousResourcesFileText;
    if (hadResourcesFile && !readUtf8FileText(resourcesFilePath, &previousResourcesFileText, &resolveError))
    {
        setLastError(resolveError);
        emit operationFailed(resolveError);
        return false;
    }

    QStringList existingResourcePaths;
    if (!loadExistingResourcePaths(resourcesFilePath, &existingResourcePaths, &resolveError))
    {
        setLastError(resolveError);
        emit operationFailed(resolveError);
        return false;
    }

    const QString resourcesDirectoryPath = resolveResourcesDirectory(m_currentHubPath, existingResourcePaths, &resolveError);
    if (resourcesDirectoryPath.isEmpty())
    {
        setLastError(resolveError);
        emit operationFailed(resolveError);
        return false;
    }

    const QStringList preflightConflictSourceFiles = conflictCheckedSourceFiles(
        sourceFiles,
        randomizeDefaultClipboardResourceNames);
    ImportConflictDescriptor conflictDescriptor;
    if (!preflightConflictSourceFiles.isEmpty()
        && !findFirstImportConflict(
            preflightConflictSourceFiles,
            resourcesDirectoryPath,
            &conflictDescriptor,
            &resolveError))
    {
        setLastError(resolveError);
        emit operationFailed(resolveError);
        return false;
    }

    const ImportConflictPolicyValue normalizedConflictPolicy = normalizedImportConflictPolicy(conflictPolicy);
    if (conflictDescriptor.valid() && normalizedConflictPolicy == ImportConflictPolicyValue::Abort)
    {
        const QString errorMessage = duplicateImportResolutionRequiredMessage(conflictDescriptor);
        setLastError(errorMessage);
        emit operationFailed(errorMessage);
        return false;
    }

    setBusy(true);
    setLastError(QString());

    QStringList importedResourcePaths;
    QStringList createdPackagePaths;
    QList<OverwrittenPackageBackup> overwrittenPackageBackups;
    importedResourcePaths.reserve(sourceFiles.size());
    createdPackagePaths.reserve(sourceFiles.size());
    QVariantList localImportedEntries;
    if (importedEntries != nullptr)
    {
        localImportedEntries.reserve(sourceFiles.size());
    }
    bool wroteResourcesFile = false;

    auto rollbackImportedResources = [&](const bool restoreResourcesFile, QString* rollbackError = nullptr)
    {
        QStringList rollbackErrors;

        for (const QString& createdPackagePath : std::as_const(createdPackagePaths))
        {
            if (!QFileInfo(createdPackagePath).exists())
            {
                continue;
            }

            if (!QDir(createdPackagePath).removeRecursively())
            {
                rollbackErrors.push_back(
                    QStringLiteral("Failed to remove imported resource package: %1").arg(createdPackagePath));
            }
        }

        for (const OverwrittenPackageBackup& backup : std::as_const(overwrittenPackageBackups))
        {
            if (!backup.packageDirectoryPath.trimmed().isEmpty()
                && QFileInfo(backup.packageDirectoryPath).exists()
                && !QDir(backup.packageDirectoryPath).removeRecursively())
            {
                rollbackErrors.push_back(
                    QStringLiteral("Failed to clear overwritten resource package: %1").arg(
                        backup.packageDirectoryPath));
            }

            if (!backup.backupDirectoryPath.trimmed().isEmpty()
                && QFileInfo(backup.backupDirectoryPath).exists()
                && !QDir().rename(backup.backupDirectoryPath, backup.packageDirectoryPath))
            {
                rollbackErrors.push_back(
                    QStringLiteral("Failed to restore overwritten resource package: %1").arg(
                        backup.packageDirectoryPath));
            }
        }

        if (restoreResourcesFile)
        {
            if (hadResourcesFile)
            {
                QString restoreError;
                if (!writeUtf8FileAtomically(resourcesFilePath, previousResourcesFileText, &restoreError))
                {
                    rollbackErrors.push_back(restoreError);
                }
            }
            else if (QFileInfo(resourcesFilePath).exists() && !QFile::remove(resourcesFilePath))
            {
                rollbackErrors.push_back(
                    QStringLiteral("Failed to remove restored Resources.tsresources file: %1").arg(resourcesFilePath));
            }
        }

        if (rollbackError != nullptr)
        {
            *rollbackError = rollbackErrors.join(QStringLiteral("; "));
        }
        return rollbackErrors.isEmpty();
    };

    for (const QString& sourceFilePath : sourceFiles)
    {
        ImportConflictDescriptor sourceFileConflictDescriptor;
        if (!shouldRandomizeDefaultClipboardResourceFile(
                sourceFilePath,
                randomizeDefaultClipboardResourceNames)
            && !findFirstImportConflict(
                QStringList{sourceFilePath},
                resourcesDirectoryPath,
                &sourceFileConflictDescriptor,
                &resolveError))
        {
            rollbackImportedResources(false, nullptr);

            setBusy(false);
            setLastError(resolveError);
            emit operationFailed(resolveError);
            return false;
        }

        QString resourcePath;
        QString packagePath;
        ThinkingSpace::Resources::ResourcePackageMetadata importedMetadata;
        QString backupDirectoryPath;
        QString importError;
        const bool shouldOverwrite =
            sourceFileConflictDescriptor.valid()
            && normalizedConflictPolicy == ImportConflictPolicyValue::Overwrite;
        const bool imported =
            shouldOverwrite
                ? overwriteSingleFile(
                    sourceFilePath,
                    sourceFileConflictDescriptor,
                    resourcesDirectoryPath,
                    &resourcePath,
                    &packagePath,
                    &importedMetadata,
                    &backupDirectoryPath,
                    &importError)
                : importSingleFile(
                    sourceFilePath,
                    resourcesDirectoryPath,
                    randomizeDefaultClipboardResourceNames,
                    &resourcePath,
                    &packagePath,
                    &importedMetadata,
                    &importError);
        if (!imported)
        {
            rollbackImportedResources(false, nullptr);

            setBusy(false);
            setLastError(importError);
            emit operationFailed(importError);
            ThinkingSpace::Debug::traceSelf(
                this,
                QString::fromLatin1(kScope),
                QStringLiteral("importUrls.failed"),
                QStringLiteral("reason=%1").arg(importError));
            return false;
        }

        importedResourcePaths.push_back(resourcePath);
        if (shouldOverwrite)
        {
            bool existingBackupTracked = false;
            for (const OverwrittenPackageBackup& backup : std::as_const(overwrittenPackageBackups))
            {
                if (backup.packageDirectoryPath == packagePath)
                {
                    existingBackupTracked = true;
                    break;
                }
            }

            if (existingBackupTracked)
            {
                if (!backupDirectoryPath.trimmed().isEmpty() && QFileInfo(backupDirectoryPath).exists())
                {
                    QDir(backupDirectoryPath).removeRecursively();
                }
            }
            else
            {
                overwrittenPackageBackups.push_back(OverwrittenPackageBackup{
                    backupDirectoryPath,
                    packagePath
                });
            }
        }
        else
        {
            createdPackagePaths.push_back(packagePath);
        }
        if (importedEntries != nullptr)
        {
            localImportedEntries.push_back(importedEntryFromMetadata(importedMetadata));
        }
    }

    QStringList mergedResourcePaths = existingResourcePaths;
    for (const QString& resourcePath : std::as_const(importedResourcePaths))
    {
        if (!mergedResourcePaths.contains(resourcePath))
        {
            mergedResourcePaths.push_back(resourcePath);
        }
    }

    ThinkingSpaceResourcesHierarchyStore store;
    store.setHubPath(m_currentHubPath);
    store.setResourcePaths(mergedResourcePaths);

    QString writeError;
    if (!store.writeToFile(resourcesFilePath, &writeError))
    {
        rollbackImportedResources(false, nullptr);

        setBusy(false);
        setLastError(writeError);
        emit operationFailed(writeError);
        ThinkingSpace::Debug::traceSelf(
            this,
            QString::fromLatin1(kScope),
            QStringLiteral("importUrls.failed"),
            QStringLiteral("reason=%1").arg(writeError));
        return false;
    }
    wroteResourcesFile = true;

    if (reloadRuntime && m_reloadResourcesCallback)
    {
        QString reloadError;
        if (!m_reloadResourcesCallback(m_currentHubPath, &reloadError))
        {
            QString rollbackError;
            rollbackImportedResources(wroteResourcesFile, &rollbackError);
            const QString errorMessage = reloadError.trimmed().isEmpty()
                                             ? QStringLiteral("Imported resources but failed to refresh the workspace.")
                                             : QStringLiteral(
                                                   "Imported resources but failed to refresh the workspace: %1").arg(
                                                   reloadError.trimmed());
            const QString finalErrorMessage = rollbackError.trimmed().isEmpty()
                                                  ? errorMessage
                                                  : QStringLiteral("%1 Rollback: %2").arg(
                                                      errorMessage,
                                                      rollbackError.trimmed());
            setBusy(false);
            setLastError(finalErrorMessage);
            emit operationFailed(finalErrorMessage);
            ThinkingSpace::Debug::traceSelf(
                this,
                QString::fromLatin1(kScope),
                QStringLiteral("importUrls.reloadFailed"),
                QStringLiteral("reason=%1").arg(finalErrorMessage));
            return false;
        }
    }

    setBusy(false);
    setLastError(QString());
    for (const OverwrittenPackageBackup& backup : std::as_const(overwrittenPackageBackups))
    {
        if (!backup.backupDirectoryPath.trimmed().isEmpty() && QFileInfo(backup.backupDirectoryPath).exists())
        {
            QDir(backup.backupDirectoryPath).removeRecursively();
        }
    }
    emit importCompleted(importedResourcePaths.size());
    ThinkingSpace::Debug::traceSelf(
        this,
        QString::fromLatin1(kScope),
        importedEntries == nullptr
            ? QStringLiteral("importUrls.success")
            : QStringLiteral("importUrlsForEditor.success"),
        QStringLiteral("importedCount=%1").arg(importedResourcePaths.size()));
    if (importedEntries != nullptr)
    {
        *importedEntries = localImportedEntries;
    }
    return true;
}

bool InAppClipboardManager::importDroppedUrls(const QVariantList& urls)
{
    return importUrlsWithConflictPolicy(urls, ConflictPolicyAbort);
}

bool InAppClipboardManager::reloadImportedResources()
{
    if (!m_reloadResourcesCallback)
    {
        return true;
    }

    if (m_busy)
    {
        const QString errorMessage = QStringLiteral("Resource import is already running.");
        setLastError(errorMessage);
        emit operationFailed(errorMessage);
        return false;
    }

    if (m_currentHubPath.trimmed().isEmpty())
    {
        const QString errorMessage = QStringLiteral("Current hub path is empty.");
        setLastError(errorMessage);
        emit operationFailed(errorMessage);
        return false;
    }

    QString reloadError;
    if (!m_reloadResourcesCallback(m_currentHubPath, &reloadError))
    {
        const QString errorMessage = reloadError.trimmed().isEmpty()
            ? QStringLiteral("Failed to refresh imported resources.")
            : QStringLiteral("Failed to refresh imported resources: %1").arg(reloadError.trimmed());
        setLastError(errorMessage);
        emit operationFailed(errorMessage);
        ThinkingSpace::Debug::traceSelf(
            this,
            QString::fromLatin1(kScope),
            QStringLiteral("reloadImportedResources.failed"),
            QStringLiteral("reason=%1").arg(errorMessage));
        return false;
    }

    setLastError(QString());
    ThinkingSpace::Debug::traceSelf(
        this,
        QString::fromLatin1(kScope),
        QStringLiteral("reloadImportedResources.success"),
        QStringLiteral("hubPath=%1").arg(m_currentHubPath));
    return true;
}

void InAppClipboardManager::setBusy(const bool busy)
{
    if (m_busy == busy)
    {
        return;
    }

    m_busy = busy;
    emit busyChanged();
}

void InAppClipboardManager::setLastError(QString errorMessage)
{
    errorMessage = errorMessage.trimmed();
    if (m_lastError == errorMessage)
    {
        return;
    }

    m_lastError = std::move(errorMessage);
    emit lastErrorChanged();
}
