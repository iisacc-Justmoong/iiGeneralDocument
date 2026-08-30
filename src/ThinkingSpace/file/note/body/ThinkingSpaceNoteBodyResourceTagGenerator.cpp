#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyResourceTagGenerator.hpp"

#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcePackageSupport.hpp"

#include <QStringList>

namespace
{
    QString decodedTrimmedString(const QVariantMap& resourceEntry, const QStringList& keyCandidates)
    {
        for (const QString& key : keyCandidates)
        {
            const QString value = ThinkingSpace::Resources::decodeXmlEntities(
                resourceEntry.value(key).toString()).trimmed();
            if (!value.isEmpty())
            {
                return value;
            }
        }

        return {};
    }

    QString escapedXmlAttributeValue(QString value)
    {
        value.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
        value.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
        value.replace(QStringLiteral("'"), QStringLiteral("&apos;"));
        value.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
        value.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
        return value;
    }
} // namespace

namespace ThinkingSpace::NoteBodyResourceTagGenerator
{
    QVariantMap normalizeImportedResourceDescriptor(const QVariantMap& resourceEntry)
    {
        const QString resourcePath = ThinkingSpace::Resources::normalizePath(
            decodedTrimmedString(
                resourceEntry,
                QStringList{
                    QStringLiteral("resourcePath"),
                    QStringLiteral("path"),
                    QStringLiteral("src"),
                    QStringLiteral("href"),
                    QStringLiteral("url")
                }));

        QString resourceFormat = ThinkingSpace::Resources::normalizeFormat(
            decodedTrimmedString(
                resourceEntry,
                QStringList{
                    QStringLiteral("format"),
                    QStringLiteral("resourceFormat"),
                    QStringLiteral("ext"),
                    QStringLiteral("extension")
                }));
        if (resourceFormat.isEmpty())
        {
            const QString formatProbe = decodedTrimmedString(
                resourceEntry,
                QStringList{
                    QStringLiteral("assetPath"),
                    QStringLiteral("resourcePath"),
                    QStringLiteral("path")
                });
            resourceFormat = ThinkingSpace::Resources::normalizeFormat(
                ThinkingSpace::Resources::formatFromAssetFilePath(formatProbe));
        }
        if (resourceFormat.isEmpty())
        {
            resourceFormat = QStringLiteral(".bin");
        }

        const QString resourceType = ThinkingSpace::Resources::normalizedTypeFromBucketAndFormat(
            decodedTrimmedString(
                resourceEntry,
                QStringList{
                    QStringLiteral("type"),
                    QStringLiteral("resourceType"),
                    QStringLiteral("mime")
                }),
            decodedTrimmedString(resourceEntry, QStringList{QStringLiteral("bucket")}),
            resourceFormat);
        const QString normalizedType = resourceType.isEmpty()
            ? QStringLiteral("other")
            : resourceType;
        const QString normalizedBucket = ThinkingSpace::Resources::normalizedBucketFromTypeAndFormat(
            decodedTrimmedString(resourceEntry, QStringList{QStringLiteral("bucket")}),
            normalizedType,
            resourceFormat);
        const QString resourceId = decodedTrimmedString(
            resourceEntry,
            QStringList{
                QStringLiteral("resourceId"),
                QStringLiteral("id")
            });

        QVariantMap descriptor;
        descriptor.insert(QStringLiteral("valid"), !resourcePath.isEmpty());
        descriptor.insert(QStringLiteral("resourceId"), resourceId);
        descriptor.insert(QStringLiteral("id"), resourceId);
        descriptor.insert(QStringLiteral("resourcePath"), resourcePath);
        descriptor.insert(QStringLiteral("path"), resourcePath);
        descriptor.insert(QStringLiteral("type"), normalizedType);
        descriptor.insert(QStringLiteral("format"), resourceFormat);
        descriptor.insert(QStringLiteral("bucket"), normalizedBucket);
        return descriptor;
    }

    QString buildCanonicalResourceTag(const QVariantMap& resourceEntry)
    {
        const QVariantMap descriptor = normalizeImportedResourceDescriptor(resourceEntry);
        const QString resourcePath = descriptor.value(QStringLiteral("resourcePath")).toString().trimmed();
        if (resourcePath.isEmpty())
        {
            return {};
        }

        QStringList attributes;
        attributes.push_back(
            QStringLiteral("type=\"%1\"").arg(
                escapedXmlAttributeValue(descriptor.value(QStringLiteral("type")).toString().trimmed())));
        attributes.push_back(
            QStringLiteral("format=\"%1\"").arg(
                escapedXmlAttributeValue(descriptor.value(QStringLiteral("format")).toString().trimmed())));
        attributes.push_back(
            QStringLiteral("path=\"%1\"").arg(
                escapedXmlAttributeValue(resourcePath)));

        const QString resourceId = descriptor.value(QStringLiteral("resourceId")).toString().trimmed();
        if (!resourceId.isEmpty())
        {
            attributes.push_back(
                QStringLiteral("id=\"%1\"").arg(
                    escapedXmlAttributeValue(resourceId)));
        }

        return QStringLiteral("<resource %1 />").arg(attributes.join(QLatin1Char(' ')));
    }
} // namespace ThinkingSpace::NoteBodyResourceTagGenerator
