#include "ThinkingSpace/platform/Android/ThinkingSpaceAndroidStorageBackend.hpp"

#include "ThinkingSpace/file/hub/ThinkingSpaceHubPathUtils.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#include <memory>

namespace ThinkingSpace::Android::Storage
{
    namespace
    {
        constexpr auto kBridgeClassName = "com/iisacc/app/thinkingspace/ThinkingSpaceAndroidStorage";
        constexpr auto kMountsDirectoryName = "android-document-mounts";
        constexpr auto kMountedHubMetadataSuffix = ".thinkingspace-android-mount.json";

        std::shared_ptr<Bridge>& testingBridge()
        {
            static std::shared_ptr<Bridge> bridge;
            return bridge;
        }

        bool isAndroidContentUri(const QString& path)
        {
            const QString trimmedPath = path.trimmed();
            if (trimmedPath.startsWith(QStringLiteral("content:"), Qt::CaseInsensitive))
            {
                return true;
            }

            const QString normalizedPath = ThinkingSpace::HubPath::normalizePath(path);
            if (normalizedPath.startsWith(QStringLiteral("content:"), Qt::CaseInsensitive))
            {
                return true;
            }

            if (!ThinkingSpace::HubPath::isNonLocalUrl(normalizedPath))
            {
                return false;
            }

            const QUrl url(normalizedPath);
            return url.isValid() && url.scheme().compare(QStringLiteral("content"), Qt::CaseInsensitive) == 0;
        }

        QString appDataRootPath()
        {
            const QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).trimmed();
            if (!appDataLocation.isEmpty())
            {
                return QDir::cleanPath(appDataLocation);
            }

            return QDir::cleanPath(QDir::currentPath());
        }

        QString mountsRootPath()
        {
            return QDir(appDataRootPath()).filePath(QString::fromLatin1(kMountsDirectoryName));
        }

        QString mountContainerPathForSource(const QString& sourceUri)
        {
            const QByteArray digest = QCryptographicHash::hash(
                ThinkingSpace::HubPath::normalizePath(sourceUri).toUtf8(),
                QCryptographicHash::Sha256).toHex();
            return QDir(mountsRootPath()).filePath(QString::fromUtf8(digest.left(12)));
        }

        QString sanitizedMountDirectoryName(QString value)
        {
            value = value.trimmed();
            if (value.isEmpty())
            {
                value = QStringLiteral("MountedHub.tshub");
            }

            if (!value.endsWith(QStringLiteral(".tshub"), Qt::CaseInsensitive))
            {
                value += QStringLiteral(".tshub");
            }

            value.replace(QLatin1Char('\\'), QLatin1Char('-'));
            value.replace(QLatin1Char('/'), QLatin1Char('-'));
            value.replace(QLatin1Char(':'), QLatin1Char('-'));
            value.replace(QLatin1Char('*'), QLatin1Char('-'));
            value.replace(QLatin1Char('?'), QLatin1Char('-'));
            value.replace(QLatin1Char('"'), QLatin1Char('-'));
            value.replace(QLatin1Char('<'), QLatin1Char('-'));
            value.replace(QLatin1Char('>'), QLatin1Char('-'));
            value.replace(QLatin1Char('|'), QLatin1Char('-'));
            return value;
        }

        QString mountDirectoryPathForSource(const QString& sourceUri, const QString& displayName)
        {
            return QDir(mountContainerPathForSource(sourceUri)).filePath(sanitizedMountDirectoryName(displayName));
        }

        QString mountMetadataPath(const QString& mountedHubPath)
        {
            const QString normalizedPath = ThinkingSpace::HubPath::normalizeAbsolutePath(mountedHubPath);
            if (normalizedPath.isEmpty())
            {
                return {};
            }

            return normalizedPath + QString::fromLatin1(kMountedHubMetadataSuffix);
        }

        bool ensureLocalDirectory(const QString& directoryPath, QString* errorMessage)
        {
            if (directoryPath.trimmed().isEmpty())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("directoryPath must not be empty.");
                }
                return false;
            }

            if (QDir().mkpath(directoryPath))
            {
                return true;
            }

            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to create directory: %1").arg(directoryPath);
            }
            return false;
        }

        bool removeLocalRecursively(const QString& path, QString* errorMessage)
        {
            const QFileInfo info(path);
            if (!info.exists())
            {
                return true;
            }

            if (info.isDir())
            {
                QDir dir(path);
                if (dir.removeRecursively())
                {
                    return true;
                }
            }
            else
            {
                QFile file(path);
                if (file.remove())
                {
                    return true;
                }
            }

            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to remove local path: %1").arg(path);
            }
            return false;
        }

        bool writeMountMetadata(
            const QString& mountedHubPath,
            const QString& sourceUri,
            const QString& displayName,
            QString* errorMessage)
        {
            QJsonObject root;
            root.insert(QStringLiteral("version"), 1);
            root.insert(QStringLiteral("schema"), QStringLiteral("thinkingspace.android.mount"));
            root.insert(QStringLiteral("mountedHubPath"), ThinkingSpace::HubPath::normalizeAbsolutePath(mountedHubPath));
            root.insert(QStringLiteral("sourceUri"), ThinkingSpace::HubPath::normalizePath(sourceUri));
            root.insert(QStringLiteral("displayName"), displayName.trimmed());

            QSaveFile file(mountMetadataPath(mountedHubPath));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Failed to open Android mount metadata file: %1 (%2)")
                                        .arg(file.fileName(), file.errorString());
                }
                return false;
            }

            if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0)
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Failed to write Android mount metadata file: %1 (%2)")
                                        .arg(file.fileName(), file.errorString());
                }
                file.cancelWriting();
                return false;
            }

            if (!file.commit())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Failed to commit Android mount metadata file: %1 (%2)")
                                        .arg(file.fileName(), file.errorString());
                }
                return false;
            }

            return true;
        }

        QString readMountMetadataSourceUri(const QString& mountedHubPath)
        {
            QFile file(mountMetadataPath(mountedHubPath));
            if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                return {};
            }

            const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
            if (!document.isObject())
            {
                return {};
            }

            return ThinkingSpace::HubPath::normalizePath(document.object().value(QStringLiteral("sourceUri")).toString());
        }

        class UnsupportedBridge final : public Bridge
        {
        public:
            bool stat(const QString&, EntryMetadata*, QString* errorMessage) override
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Android document storage backend is unavailable on this platform.");
                }
                return false;
            }

            bool listChildren(const QString&, QVector<EntryMetadata>*, QString* errorMessage) override
            {
                return stat({}, nullptr, errorMessage);
            }

            bool createDirectory(const QString&, const QString&, QString*, QString* errorMessage) override
            {
                return stat({}, nullptr, errorMessage);
            }

            bool createFile(const QString&, const QString&, QString*, QString* errorMessage) override
            {
                return stat({}, nullptr, errorMessage);
            }

            bool readBytes(const QString&, QByteArray*, QString* errorMessage) override
            {
                return stat({}, nullptr, errorMessage);
            }

            bool writeBytes(const QString&, const QByteArray&, QString* errorMessage) override
            {
                return stat({}, nullptr, errorMessage);
            }
        };

#if defined(Q_OS_ANDROID)
        class JniBridge final : public Bridge
        {
        public:
            bool stat(const QString& uri, EntryMetadata* outEntry, QString* errorMessage) override
            {
                const QJsonObject object = callJsonMethod(
                    "statJson",
                    uri,
                    errorMessage);
                if (object.isEmpty())
                {
                    return false;
                }

                if (outEntry != nullptr)
                {
                    outEntry->uri = object.value(QStringLiteral("uri")).toString().trimmed();
                    outEntry->name = object.value(QStringLiteral("name")).toString().trimmed();
                    outEntry->exists = object.value(QStringLiteral("exists")).toBool(false);
                    outEntry->directory = object.value(QStringLiteral("directory")).toBool(false);
                    outEntry->file = object.value(QStringLiteral("file")).toBool(false);
                }
                return true;
            }

            bool listChildren(const QString& uri, QVector<EntryMetadata>* outEntries, QString* errorMessage) override
            {
                if (outEntries == nullptr)
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("outEntries must not be null.");
                    }
                    return false;
                }

                const QJsonObject object = callJsonMethod(
                    "listChildrenJson",
                    uri,
                    errorMessage);
                if (object.isEmpty())
                {
                    return false;
                }

                outEntries->clear();
                const QJsonArray entries = object.value(QStringLiteral("entries")).toArray();
                outEntries->reserve(entries.size());
                for (const QJsonValue& value : entries)
                {
                    if (!value.isObject())
                    {
                        continue;
                    }

                    const QJsonObject entryObject = value.toObject();
                    EntryMetadata entry;
                    entry.uri = entryObject.value(QStringLiteral("uri")).toString().trimmed();
                    entry.name = entryObject.value(QStringLiteral("name")).toString().trimmed();
                    entry.exists = entryObject.value(QStringLiteral("exists")).toBool(false);
                    entry.directory = entryObject.value(QStringLiteral("directory")).toBool(false);
                    entry.file = entryObject.value(QStringLiteral("file")).toBool(false);
                    outEntries->push_back(std::move(entry));
                }
                return true;
            }

            bool createDirectory(
                const QString& parentUri,
                const QString& name,
                QString* outChildUri,
                QString* errorMessage) override
            {
                return createEntry("createDirectoryJson", parentUri, name, outChildUri, errorMessage);
            }

            bool createFile(
                const QString& parentUri,
                const QString& name,
                QString* outChildUri,
                QString* errorMessage) override
            {
                return createEntry("createFileJson", parentUri, name, outChildUri, errorMessage);
            }

            bool readBytes(const QString& uri, QByteArray* outBytes, QString* errorMessage) override
            {
                if (outBytes == nullptr)
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("outBytes must not be null.");
                    }
                    return false;
                }

                const QJsonObject object = callJsonMethod(
                    "readBytesBase64Json",
                    uri,
                    errorMessage);
                if (object.isEmpty())
                {
                    return false;
                }

                *outBytes = QByteArray::fromBase64(
                    object.value(QStringLiteral("base64")).toString().toUtf8());
                return true;
            }

            bool writeBytes(const QString& uri, const QByteArray& bytes, QString* errorMessage) override
            {
                const QJniObject context = QNativeInterface::QAndroidApplication::context();
                if (!context.isValid())
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Android application context is unavailable.");
                    }
                    return false;
                }

                const QJniObject uriObject = QJniObject::fromString(uri);
                const QJniObject base64Object = QJniObject::fromString(QString::fromUtf8(bytes.toBase64()));
                const jstring result = QJniObject::callStaticObjectMethod<jstring>(
                    kBridgeClassName,
                    "writeBytesBase64Json",
                    "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                    context.object<jobject>(),
                    uriObject.object<jstring>(),
                    base64Object.object<jstring>());
                return parseOkEnvelope(QJniObject(result).toString(), errorMessage);
            }

        private:
            QJsonObject callJsonMethod(
                const char* methodName,
                const QString& uri,
                QString* errorMessage) const
            {
                const QJniObject context = QNativeInterface::QAndroidApplication::context();
                if (!context.isValid())
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Android application context is unavailable.");
                    }
                    return {};
                }

                const QJniObject uriObject = QJniObject::fromString(uri);
                const jstring result = QJniObject::callStaticObjectMethod<jstring>(
                    kBridgeClassName,
                    methodName,
                    "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
                    context.object<jobject>(),
                    uriObject.object<jstring>());
                return parseJsonEnvelope(QJniObject(result).toString(), errorMessage);
            }

            bool createEntry(
                const char* methodName,
                const QString& parentUri,
                const QString& name,
                QString* outChildUri,
                QString* errorMessage) const
            {
                const QJniObject context = QNativeInterface::QAndroidApplication::context();
                if (!context.isValid())
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Android application context is unavailable.");
                    }
                    return false;
                }

                const QJniObject parentUriObject = QJniObject::fromString(parentUri);
                const QJniObject nameObject = QJniObject::fromString(name);
                const jstring result = QJniObject::callStaticObjectMethod<jstring>(
                    kBridgeClassName,
                    methodName,
                    "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                    context.object<jobject>(),
                    parentUriObject.object<jstring>(),
                    nameObject.object<jstring>());
                const QJsonObject object = parseJsonEnvelope(QJniObject(result).toString(), errorMessage);
                if (object.isEmpty())
                {
                    return false;
                }

                if (outChildUri != nullptr)
                {
                    *outChildUri = object.value(QStringLiteral("uri")).toString().trimmed();
                }
                return true;
            }

            static QJsonObject parseJsonEnvelope(const QString& jsonText, QString* errorMessage)
            {
                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
                if (parseError.error != QJsonParseError::NoError || !document.isObject())
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Invalid Android storage backend response.");
                    }
                    return {};
                }

                const QJsonObject object = document.object();
                if (!object.value(QStringLiteral("ok")).toBool(false))
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = object.value(QStringLiteral("error")).toString().trimmed();
                    }
                    return {};
                }

                return object;
            }

            static bool parseOkEnvelope(const QString& jsonText, QString* errorMessage)
            {
                return !parseJsonEnvelope(jsonText, errorMessage).isEmpty();
            }
        };
#endif

        Bridge& activeBridge()
        {
            if (testingBridge())
            {
                return *testingBridge();
            }

#if defined(Q_OS_ANDROID)
            static JniBridge bridge;
            return bridge;
#else
            static UnsupportedBridge bridge;
            return bridge;
#endif
        }

        bool statEntry(const QString& uri, EntryMetadata* outEntry, QString* errorMessage)
        {
            return activeBridge().stat(ThinkingSpace::HubPath::normalizePath(uri), outEntry, errorMessage);
        }

        bool listChildren(const QString& uri, QVector<EntryMetadata>* outEntries, QString* errorMessage)
        {
            return activeBridge().listChildren(ThinkingSpace::HubPath::normalizePath(uri), outEntries, errorMessage);
        }

        QString uniqueDirectoryNameForParent(
            const QString& parentUri,
            const QString& preferredName,
            QString* errorMessage)
        {
            QVector<EntryMetadata> childEntries;
            if (!listChildren(parentUri, &childEntries, errorMessage))
            {
                return {};
            }

            QString candidateName = preferredName.trimmed();
            if (candidateName.isEmpty())
            {
                candidateName = QStringLiteral("Untitled.tshub");
            }
            if (!candidateName.endsWith(QStringLiteral(".tshub"), Qt::CaseInsensitive))
            {
                candidateName += QStringLiteral(".tshub");
            }
            const QFileInfo preferredInfo(candidateName);
            const QString preferredBaseName = preferredInfo.completeBaseName();
            const QString suffix = preferredInfo.completeSuffix().trimmed();
            const QString normalizedSuffix = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;

            auto nameExists = [&childEntries](const QString& candidate) {
                return std::any_of(
                    childEntries.cbegin(),
                    childEntries.cend(),
                    [&candidate](const EntryMetadata& entry)
                    {
                        return entry.directory && entry.name.compare(candidate, Qt::CaseInsensitive) == 0;
                    });
            };

            int candidateIndex = 2;
            while (nameExists(candidateName))
            {
                candidateName = QStringLiteral("%1-%2%3")
                                    .arg(preferredBaseName)
                                    .arg(candidateIndex)
                                    .arg(normalizedSuffix);
                ++candidateIndex;
            }

            return candidateName;
        }

        bool createDirectoryEntry(
            const QString& parentUri,
            const QString& name,
            QString* outChildUri,
            QString* errorMessage)
        {
            return activeBridge().createDirectory(
                ThinkingSpace::HubPath::normalizePath(parentUri),
                name,
                outChildUri,
                errorMessage);
        }

        bool createFileEntry(
            const QString& parentUri,
            const QString& name,
            QString* outChildUri,
            QString* errorMessage)
        {
            return activeBridge().createFile(
                ThinkingSpace::HubPath::normalizePath(parentUri),
                name,
                outChildUri,
                errorMessage);
        }

        bool readBytes(const QString& uri, QByteArray* outBytes, QString* errorMessage)
        {
            return activeBridge().readBytes(ThinkingSpace::HubPath::normalizePath(uri), outBytes, errorMessage);
        }

        bool writeBytes(const QString& uri, const QByteArray& bytes, QString* errorMessage)
        {
            return activeBridge().writeBytes(ThinkingSpace::HubPath::normalizePath(uri), bytes, errorMessage);
        }

        bool findChildEntry(
            const QString& parentUri,
            const QString& name,
            EntryMetadata* outEntry,
            QString* errorMessage)
        {
            if (outEntry != nullptr)
            {
                *outEntry = EntryMetadata();
            }
            if (errorMessage != nullptr)
            {
                errorMessage->clear();
            }

            QVector<EntryMetadata> children;
            if (!listChildren(parentUri, &children, errorMessage))
            {
                return false;
            }

            const QString trimmedName = name.trimmed();
            for (const EntryMetadata& child : children)
            {
                if (child.name.compare(trimmedName, Qt::CaseInsensitive) != 0)
                {
                    continue;
                }

                if (outEntry != nullptr)
                {
                    *outEntry = child;
                }
                return true;
            }

            return false;
        }

        bool ensureDirectoryEntry(
            const QString& parentUri,
            const QString& name,
            QString* outChildUri,
            QString* errorMessage)
        {
            EntryMetadata existingChild;
            QString lookupError;
            if (findChildEntry(parentUri, name, &existingChild, &lookupError))
            {
                if (!existingChild.directory)
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Existing Android document is not a directory: %1").arg(name);
                    }
                    return false;
                }

                if (outChildUri != nullptr)
                {
                    *outChildUri = existingChild.uri;
                }
                return true;
            }
            if (!lookupError.trimmed().isEmpty())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = lookupError;
                }
                return false;
            }

            return createDirectoryEntry(parentUri, name, outChildUri, errorMessage);
        }

        bool ensureFileEntry(
            const QString& parentUri,
            const QString& name,
            QString* outChildUri,
            QString* errorMessage)
        {
            EntryMetadata existingChild;
            QString lookupError;
            if (findChildEntry(parentUri, name, &existingChild, &lookupError))
            {
                if (!existingChild.file)
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Existing Android document is not a file: %1").arg(name);
                    }
                    return false;
                }

                if (outChildUri != nullptr)
                {
                    *outChildUri = existingChild.uri;
                }
                return true;
            }
            if (!lookupError.trimmed().isEmpty())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = lookupError;
                }
                return false;
            }

            return createFileEntry(parentUri, name, outChildUri, errorMessage);
        }

        bool writeLocalFileToSource(
            const QString& localFilePath,
            const QString& sourceParentUri,
            QString* errorMessage)
        {
            const QFileInfo fileInfo(localFilePath);
            if (!fileInfo.exists() || !fileInfo.isFile())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Local file does not exist: %1").arg(localFilePath);
                }
                return false;
            }

            QString childFileUri;
            if (!ensureFileEntry(sourceParentUri, fileInfo.fileName(), &childFileUri, errorMessage))
            {
                return false;
            }

            QFile file(localFilePath);
            if (!file.open(QIODevice::ReadOnly))
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Failed to read local hub file: %1 (%2)")
                                        .arg(localFilePath, file.errorString());
                }
                return false;
            }

            return writeBytes(childFileUri, file.readAll(), errorMessage);
        }

        bool syncSourceDirectoryToLocal(
            const QString& sourceUri,
            const QString& localDirectoryPath,
            QString* errorMessage)
        {
            QVector<EntryMetadata> children;
            if (!listChildren(sourceUri, &children, errorMessage))
            {
                return false;
            }

            if (!ensureLocalDirectory(localDirectoryPath, errorMessage))
            {
                return false;
            }

            for (const EntryMetadata& child : children)
            {
                const QString childLocalPath = QDir(localDirectoryPath).filePath(child.name);
                if (child.directory)
                {
                    if (!syncSourceDirectoryToLocal(child.uri, childLocalPath, errorMessage))
                    {
                        return false;
                    }
                    continue;
                }

                if (!child.file)
                {
                    continue;
                }

                QByteArray fileBytes;
                if (!readBytes(child.uri, &fileBytes, errorMessage))
                {
                    return false;
                }

                QFile file(childLocalPath);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Failed to materialize mounted file: %1 (%2)")
                                            .arg(childLocalPath, file.errorString());
                    }
                    return false;
                }

                if (file.write(fileBytes) != fileBytes.size())
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Failed to write mounted file bytes: %1 (%2)")
                                            .arg(childLocalPath, file.errorString());
                    }
                    return false;
                }
            }

            return true;
        }

        bool syncLocalDirectoryToSource(
            const QString& localDirectoryPath,
            const QString& sourceDirectoryUri,
            QString* errorMessage)
        {
            const QFileInfo localInfo(localDirectoryPath);
            if (!localInfo.exists() || !localInfo.isDir())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Local hub directory does not exist: %1").arg(localDirectoryPath);
                }
                return false;
            }

            const QDir localDirectory(localDirectoryPath);
            const QFileInfoList entries = localDirectory.entryInfoList(
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                QDir::Name);
            for (const QFileInfo& entryInfo : entries)
            {
                const QString entryName = entryInfo.fileName();
                if (entryInfo.isDir())
                {
                    QString childDirectoryUri;
                    if (!ensureDirectoryEntry(sourceDirectoryUri, entryName, &childDirectoryUri, errorMessage))
                    {
                        return false;
                    }

                    if (!syncLocalDirectoryToSource(entryInfo.absoluteFilePath(), childDirectoryUri, errorMessage))
                    {
                        return false;
                    }
                    continue;
                }

                if (!writeLocalFileToSource(entryInfo.absoluteFilePath(), sourceDirectoryUri, errorMessage))
                {
                    return false;
                }
            }

            return true;
        }

        bool exportLocalDirectoryToSource(
            const QString& localDirectoryPath,
            const QString& sourceDirectoryUri,
            QString* errorMessage)
        {
            const QFileInfo localInfo(localDirectoryPath);
            if (!localInfo.exists() || !localInfo.isDir())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("Local hub directory does not exist: %1").arg(localDirectoryPath);
                }
                return false;
            }

            const QDir localDirectory(localDirectoryPath);
            const QFileInfoList entries = localDirectory.entryInfoList(
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                QDir::Name);
            for (const QFileInfo& entryInfo : entries)
            {
                const QString entryName = entryInfo.fileName();
                if (entryInfo.isDir())
                {
                    QString childDirectoryUri;
                    if (!createDirectoryEntry(sourceDirectoryUri, entryName, &childDirectoryUri, errorMessage))
                    {
                        return false;
                    }

                    if (!exportLocalDirectoryToSource(entryInfo.absoluteFilePath(), childDirectoryUri, errorMessage))
                    {
                        return false;
                    }
                    continue;
                }

                QString childFileUri;
                if (!createFileEntry(sourceDirectoryUri, entryName, &childFileUri, errorMessage))
                {
                    return false;
                }

                QFile file(entryInfo.absoluteFilePath());
                if (!file.open(QIODevice::ReadOnly))
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Failed to read local hub file: %1 (%2)")
                                            .arg(entryInfo.absoluteFilePath(), file.errorString());
                    }
                    return false;
                }

                if (!writeBytes(childFileUri, file.readAll(), errorMessage))
                {
                    return false;
                }
            }

            return true;
        }

        QString mountedHubRootForLocalPath(const QString& localPath)
        {
            QString currentPath = ThinkingSpace::HubPath::normalizeAbsolutePath(localPath);
            if (currentPath.isEmpty())
            {
                return {};
            }

            const QFileInfo currentInfo(currentPath);
            if (currentInfo.exists() && currentInfo.isFile())
            {
                currentPath = currentInfo.absolutePath();
            }

            while (!currentPath.isEmpty())
            {
                if (isMountedHubPath(currentPath))
                {
                    return currentPath;
                }

                const QString parentPath = QFileInfo(currentPath).absolutePath();
                if (parentPath.isEmpty() || parentPath == currentPath)
                {
                    break;
                }
                currentPath = parentPath;
            }

            return {};
        }
    } // namespace

    void setBridgeForTesting(std::shared_ptr<Bridge> bridge)
    {
        testingBridge() = std::move(bridge);
    }

    void clearBridgeForTesting()
    {
        testingBridge().reset();
    }

    bool isSupportedUri(const QString& path)
    {
        return isAndroidContentUri(path);
    }

    QString displayName(const QString& path)
    {
        if (!isSupportedUri(path))
        {
            return QFileInfo(ThinkingSpace::HubPath::normalizePath(path)).fileName().trimmed();
        }

        EntryMetadata entry;
        QString errorMessage;
        if (!statEntry(path, &entry, &errorMessage))
        {
            return {};
        }
        return entry.name;
    }

    bool resolveHubSelection(
        const QString& selectedPath,
        QStringList* outHubUris,
        QString* errorMessage)
    {
        if (outHubUris == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("outHubUris must not be null.");
            }
            return false;
        }

        outHubUris->clear();

        const QString normalizedSelectedPath = ThinkingSpace::HubPath::normalizePath(selectedPath);
        if (!isSupportedUri(normalizedSelectedPath))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Selected path is not a supported Android document URI.");
            }
            return false;
        }

        EntryMetadata selectedEntry;
        if (!statEntry(normalizedSelectedPath, &selectedEntry, errorMessage))
        {
            return false;
        }

        if (!selectedEntry.exists)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Selected Android document does not exist: %1").arg(normalizedSelectedPath);
            }
            return false;
        }

        if (selectedEntry.directory && selectedEntry.name.endsWith(QStringLiteral(".tshub"), Qt::CaseInsensitive))
        {
            outHubUris->push_back(selectedEntry.uri);
            return true;
        }

        if (!selectedEntry.directory)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral(
                    "Selected Android document is not a mountable Thinking Space Hub directory: %1").arg(selectedEntry.name);
            }
            return false;
        }

        QVector<EntryMetadata> childEntries;
        if (!listChildren(selectedEntry.uri, &childEntries, errorMessage))
        {
            return false;
        }

        for (const EntryMetadata& childEntry : childEntries)
        {
            if (!childEntry.directory)
            {
                continue;
            }
            if (!childEntry.name.endsWith(QStringLiteral(".tshub"), Qt::CaseInsensitive))
            {
                continue;
            }
            outHubUris->push_back(childEntry.uri);
        }

        if (!outHubUris->isEmpty())
        {
            return true;
        }

        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral(
                "Selected Android folder does not contain a Thinking Space Hub package directory.");
        }
        return false;
    }

    bool mountHub(
        const QString& hubUri,
        QString* outMountedHubPath,
        QString* errorMessage)
    {
        const QString normalizedHubUri = ThinkingSpace::HubPath::normalizePath(hubUri);
        if (!isSupportedUri(normalizedHubUri))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Hub URI is not a supported Android document URI.");
            }
            return false;
        }

        EntryMetadata hubEntry;
        if (!statEntry(normalizedHubUri, &hubEntry, errorMessage))
        {
            return false;
        }

        if (!hubEntry.exists || !hubEntry.directory)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("The selected Thinking Space Hub is not a directory-backed Android document.");
            }
            return false;
        }
        if (!hubEntry.name.endsWith(QStringLiteral(".tshub"), Qt::CaseInsensitive))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("The selected Android document is not a .tshub package.");
            }
            return false;
        }

        const QString mountedHubPath = mountDirectoryPathForSource(hubEntry.uri, hubEntry.name);
        const QString mountContainerPath = mountContainerPathForSource(hubEntry.uri);
        QString cleanupError;
        if (!removeLocalRecursively(mountContainerPath, &cleanupError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = cleanupError;
            }
            return false;
        }

        if (!ensureLocalDirectory(mountedHubPath, errorMessage))
        {
            return false;
        }

        if (!syncSourceDirectoryToLocal(hubEntry.uri, mountedHubPath, errorMessage))
        {
            removeLocalRecursively(mountedHubPath, nullptr);
            return false;
        }

        if (!writeMountMetadata(mountedHubPath, hubEntry.uri, hubEntry.name, errorMessage))
        {
            return false;
        }

        if (outMountedHubPath != nullptr)
        {
            *outMountedHubPath = ThinkingSpace::HubPath::normalizeAbsolutePath(mountedHubPath);
        }
        return true;
    }

    bool exportLocalHubToDirectory(
        const QString& localHubPath,
        const QString& directoryUri,
        QString* outMountedHubPath,
        QString* errorMessage)
    {
        const QString normalizedLocalHubPath = ThinkingSpace::HubPath::normalizeAbsolutePath(localHubPath);
        const QString normalizedDirectoryUri = ThinkingSpace::HubPath::normalizePath(directoryUri);

        if (!isSupportedUri(normalizedDirectoryUri))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Target directory is not a supported Android document URI.");
            }
            return false;
        }

        const QFileInfo localHubInfo(normalizedLocalHubPath);
        if (!localHubInfo.exists() || !localHubInfo.isDir())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Local Thinking Space Hub scaffold does not exist: %1").arg(
                    normalizedLocalHubPath);
            }
            return false;
        }

        EntryMetadata parentEntry;
        if (!statEntry(normalizedDirectoryUri, &parentEntry, errorMessage))
        {
            return false;
        }

        if (!parentEntry.exists || !parentEntry.directory)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Selected Android destination is not a directory.");
            }
            return false;
        }

        QString sourceHubUri;
        const QString sourceHubDirectoryName = uniqueDirectoryNameForParent(
            parentEntry.uri,
            localHubInfo.fileName(),
            errorMessage);
        if (sourceHubDirectoryName.isEmpty())
        {
            return false;
        }

        if (!createDirectoryEntry(parentEntry.uri, sourceHubDirectoryName, &sourceHubUri, errorMessage))
        {
            return false;
        }

        if (!exportLocalDirectoryToSource(normalizedLocalHubPath, sourceHubUri, errorMessage))
        {
            return false;
        }

        const QString mountedHubPath = mountDirectoryPathForSource(sourceHubUri, sourceHubDirectoryName);
        const QString mountContainerPath = mountContainerPathForSource(sourceHubUri);
        QString cleanupError;
        if (!removeLocalRecursively(mountContainerPath, &cleanupError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = cleanupError;
            }
            return false;
        }

        if (!ensureLocalDirectory(QFileInfo(mountedHubPath).absolutePath(), errorMessage))
        {
            return false;
        }

        if (!QDir().rename(normalizedLocalHubPath, mountedHubPath))
        {
            if (!ensureLocalDirectory(mountedHubPath, errorMessage))
            {
                return false;
            }

            QDirIterator iterator(
                normalizedLocalHubPath,
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                QDirIterator::Subdirectories);
            while (iterator.hasNext())
            {
                iterator.next();
                const QFileInfo entryInfo = iterator.fileInfo();
                const QString relativePath = QDir(normalizedLocalHubPath).relativeFilePath(entryInfo.absoluteFilePath());
                const QString destinationPath = QDir(mountedHubPath).filePath(relativePath);
                if (entryInfo.isDir())
                {
                    if (!ensureLocalDirectory(destinationPath, errorMessage))
                    {
                        return false;
                    }
                    continue;
                }

                if (!ensureLocalDirectory(QFileInfo(destinationPath).absolutePath(), errorMessage))
                {
                    return false;
                }
                QFile::remove(destinationPath);
                if (!QFile::copy(entryInfo.absoluteFilePath(), destinationPath))
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("Failed to materialize mounted hub file: %1").arg(destinationPath);
                    }
                    return false;
                }
            }
        }

        if (!writeMountMetadata(mountedHubPath, sourceHubUri, localHubInfo.fileName(), errorMessage))
        {
            return false;
        }

        if (outMountedHubPath != nullptr)
        {
            *outMountedHubPath = ThinkingSpace::HubPath::normalizeAbsolutePath(mountedHubPath);
        }
        return true;
    }

    bool syncLocalPathToSource(
        const QString& localPath,
        QString* errorMessage)
    {
        const QString normalizedLocalPath = ThinkingSpace::HubPath::normalizeAbsolutePath(localPath);
        if (normalizedLocalPath.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Local path must not be empty.");
            }
            return false;
        }

        const QString mountedHubPath = mountedHubRootForLocalPath(normalizedLocalPath);
        if (mountedHubPath.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                errorMessage->clear();
            }
            return true;
        }

        const QString sourceHubUri = mountedHubSourceUri(mountedHubPath);
        if (sourceHubUri.trimmed().isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Mounted Android Thinking Space Hub is missing its source URI.");
            }
            return false;
        }

        const QFileInfo localInfo(normalizedLocalPath);
        if (!localInfo.exists())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Local path does not exist: %1").arg(normalizedLocalPath);
            }
            return false;
        }

        if (normalizedLocalPath == mountedHubPath)
        {
            return syncLocalDirectoryToSource(normalizedLocalPath, sourceHubUri, errorMessage);
        }

        const QString relativePath = QDir(mountedHubPath).relativeFilePath(normalizedLocalPath);
        if (relativePath.startsWith(QStringLiteral("../")))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Local path is outside the mounted Android hub: %1").arg(
                    normalizedLocalPath);
            }
            return false;
        }

        const QStringList segments = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString sourceDirectoryUri = sourceHubUri;
        const qsizetype parentSegmentCount = std::max<qsizetype>(0, segments.size() - 1);
        for (qsizetype index = 0; index < parentSegmentCount; ++index)
        {
            if (!ensureDirectoryEntry(sourceDirectoryUri, segments.at(index), &sourceDirectoryUri, errorMessage))
            {
                return false;
            }
        }

        if (localInfo.isDir())
        {
            QString targetDirectoryUri = sourceDirectoryUri;
            if (!segments.isEmpty())
            {
                if (!ensureDirectoryEntry(sourceDirectoryUri, segments.constLast(), &targetDirectoryUri, errorMessage))
                {
                    return false;
                }
            }
            return syncLocalDirectoryToSource(normalizedLocalPath, targetDirectoryUri, errorMessage);
        }

        return writeLocalFileToSource(normalizedLocalPath, sourceDirectoryUri, errorMessage);
    }

    bool isMountedHubPath(const QString& path)
    {
        const QString normalizedPath = ThinkingSpace::HubPath::normalizeAbsolutePath(path);
        return !readMountMetadataSourceUri(normalizedPath).isEmpty();
    }

    QString mountedHubSourceUri(const QString& mountedHubPath)
    {
        return readMountMetadataSourceUri(mountedHubPath);
    }
} // namespace ThinkingSpace::Android::Storage
