#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace ThinkingSpace::Android::Storage
{
    struct EntryMetadata final
    {
        QString uri;
        QString name;
        bool exists = false;
        bool directory = false;
        bool file = false;
    };

    class Bridge
    {
    public:
        virtual ~Bridge() = default;

        virtual bool stat(const QString& uri, EntryMetadata* outEntry, QString* errorMessage) = 0;
        virtual bool listChildren(const QString& uri, QVector<EntryMetadata>* outEntries, QString* errorMessage) = 0;
        virtual bool createDirectory(
            const QString& parentUri,
            const QString& name,
            QString* outChildUri,
            QString* errorMessage) = 0;
        virtual bool createFile(
            const QString& parentUri,
            const QString& name,
            QString* outChildUri,
            QString* errorMessage) = 0;
        virtual bool readBytes(const QString& uri, QByteArray* outBytes, QString* errorMessage) = 0;
        virtual bool writeBytes(const QString& uri, const QByteArray& bytes, QString* errorMessage) = 0;
    };

    void setBridgeForTesting(std::shared_ptr<Bridge> bridge);
    void clearBridgeForTesting();

    [[nodiscard]] bool isSupportedUri(const QString& path);
    [[nodiscard]] QString displayName(const QString& path);

    bool resolveHubSelection(
        const QString& selectedPath,
        QStringList* outHubUris,
        QString* errorMessage = nullptr);

    bool mountHub(
        const QString& hubUri,
        QString* outMountedHubPath,
        QString* errorMessage = nullptr);

    bool exportLocalHubToDirectory(
        const QString& localHubPath,
        const QString& directoryUri,
        QString* outMountedHubPath,
        QString* errorMessage = nullptr);

    bool syncLocalPathToSource(
        const QString& localPath,
        QString* errorMessage = nullptr);

    [[nodiscard]] bool isMountedHubPath(const QString& path);
    [[nodiscard]] QString mountedHubSourceUri(const QString& mountedHubPath);
} // namespace ThinkingSpace::Android::Storage

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
