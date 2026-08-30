#include "ThinkingSpace/file/note/folder/ThinkingSpaceNoteFolderBindingService.hpp"

#include "ThinkingSpace/file/note/folder/ThinkingSpaceNoteFolderSemantics.hpp"
#include "ThinkingSpace/hierarchy/ThinkingSpaceFolderIdentity.hpp"

#include <QSet>

namespace
{
    QString normalizeFolderPath(QString value)
    {
        return ThinkingSpace::NoteFolders::normalizeFolderPath(std::move(value));
    }

    QString normalizeFolderLookupKey(QString value)
    {
        return normalizeFolderPath(std::move(value)).toCaseFolded();
    }

    QString normalizeFolderUuid(QString value)
    {
        return ThinkingSpace::FolderIdentity::normalizeFolderUuid(std::move(value));
    }

    void appendDistinctFolderBinding(
        ThinkingSpaceNoteFolderBindingService::Bindings* targetBindings,
        QSet<QString>* targetKeys,
        const QString& folderValue,
        const QString& folderUuid,
        bool preserveOriginalValue)
    {
        if (targetBindings == nullptr || targetKeys == nullptr)
        {
            return;
        }

        const QString trimmedValue = folderValue.trimmed();
        const QString normalizedFolderPath = normalizeFolderPath(trimmedValue);
        const QString normalizedFolderKey = normalizeFolderLookupKey(trimmedValue);
        const QString normalizedFolderUuid = normalizeFolderUuid(folderUuid);
        const QString bindingKey = !normalizedFolderUuid.isEmpty()
                                       ? QStringLiteral("uuid:%1").arg(normalizedFolderUuid)
                                       : QStringLiteral("path:%1").arg(normalizedFolderKey);
        if ((normalizedFolderUuid.isEmpty() && normalizedFolderKey.isEmpty())
            || targetKeys->contains(bindingKey))
        {
            return;
        }

        targetKeys->insert(bindingKey);
        targetBindings->folders.push_back(preserveOriginalValue ? trimmedValue : normalizedFolderPath);
        targetBindings->folderUuids.push_back(normalizedFolderUuid);
    }

    QSet<QString> bindingKeys(const ThinkingSpaceNoteFolderBindingService::Bindings& bindings)
    {
        QSet<QString> keys;
        for (int index = 0; index < bindings.folders.size(); ++index)
        {
            const QString normalizedFolderUuid = index < bindings.folderUuids.size()
                                                     ? normalizeFolderUuid(bindings.folderUuids.at(index))
                                                     : QString();
            const QString normalizedFolderKey = normalizeFolderLookupKey(bindings.folders.at(index));
            const QString bindingKey = !normalizedFolderUuid.isEmpty()
                                           ? QStringLiteral("uuid:%1").arg(normalizedFolderUuid)
                                           : QStringLiteral("path:%1").arg(normalizedFolderKey);
            if (!bindingKey.endsWith(QLatin1Char(':')))
            {
                keys.insert(bindingKey);
            }
        }
        return keys;
    }
} // namespace

ThinkingSpaceNoteFolderBindingService::ThinkingSpaceNoteFolderBindingService() = default;

ThinkingSpaceNoteFolderBindingService::~ThinkingSpaceNoteFolderBindingService() = default;

ThinkingSpaceNoteFolderBindingService::Bindings ThinkingSpaceNoteFolderBindingService::bindings(
    const QStringList& folders,
    const QStringList& folderUuids) const
{
    Bindings sanitizedBindings;
    sanitizedBindings.folders.reserve(folders.size());
    sanitizedBindings.folderUuids.reserve(folders.size());
    QSet<QString> seenBindingKeys;

    for (int index = 0; index < folders.size(); ++index)
    {
        appendDistinctFolderBinding(
            &sanitizedBindings,
            &seenBindingKeys,
            folders.at(index),
            index < folderUuids.size() ? folderUuids.at(index) : QString(),
            true);
    }

    return sanitizedBindings;
}

ThinkingSpaceNoteFolderBindingService::Bindings ThinkingSpaceNoteFolderBindingService::mergeBindings(
    const Bindings& primary,
    const Bindings& secondary) const
{
    return mergeBindings(primary.folders, primary.folderUuids, secondary.folders, secondary.folderUuids);
}

ThinkingSpaceNoteFolderBindingService::Bindings ThinkingSpaceNoteFolderBindingService::mergeBindings(
    const QStringList& primaryFolders,
    const QStringList& primaryFolderUuids,
    const QStringList& secondaryFolders,
    const QStringList& secondaryFolderUuids) const
{
    Bindings mergedBindings = bindings(primaryFolders, primaryFolderUuids);
    QSet<QString> mergedBindingKeys = bindingKeys(mergedBindings);

    for (int index = 0; index < secondaryFolders.size(); ++index)
    {
        appendDistinctFolderBinding(
            &mergedBindings,
            &mergedBindingKeys,
            secondaryFolders.at(index),
            index < secondaryFolderUuids.size() ? secondaryFolderUuids.at(index) : QString(),
            true);
    }

    return mergedBindings;
}

ThinkingSpaceNoteFolderBindingService::Bindings ThinkingSpaceNoteFolderBindingService::assignFolder(
    const Bindings& existing,
    const QString& folderPath,
    const QString& folderUuid) const
{
    Bindings assignedBindings = bindings(existing.folders, existing.folderUuids);
    const QString normalizedTargetFolderPath = normalizeFolderPath(folderPath);
    const QString normalizedTargetFolderUuid = normalizeFolderUuid(folderUuid);
    if (!normalizedTargetFolderUuid.isEmpty() && !normalizedTargetFolderPath.isEmpty())
    {
        while (assignedBindings.folderUuids.size() < assignedBindings.folders.size())
        {
            assignedBindings.folderUuids.push_back(QString());
        }

        for (int index = 0; index < assignedBindings.folders.size(); ++index)
        {
            const QString existingUuid = index < assignedBindings.folderUuids.size()
                                             ? normalizeFolderUuid(assignedBindings.folderUuids.at(index))
                                             : QString();
            if (existingUuid != normalizedTargetFolderUuid)
            {
                continue;
            }

            assignedBindings.folders[index] = normalizedTargetFolderPath;
            assignedBindings.folderUuids[index] = normalizedTargetFolderUuid;
            return assignedBindings;
        }
    }

    QSet<QString> assignedBindingKeys = bindingKeys(assignedBindings);
    appendDistinctFolderBinding(
        &assignedBindings,
        &assignedBindingKeys,
        folderPath,
        folderUuid,
        false);
    return assignedBindings;
}

bool ThinkingSpaceNoteFolderBindingService::contains(
    const Bindings& bindings,
    const QString& folderPath,
    const QString& folderUuid) const
{
    const QString normalizedFolderUuid = normalizeFolderUuid(folderUuid);
    const QString normalizedFolderKey = normalizeFolderLookupKey(folderPath);

    for (int index = 0; index < bindings.folders.size(); ++index)
    {
        const QString existingUuid = index < bindings.folderUuids.size()
                                         ? normalizeFolderUuid(bindings.folderUuids.at(index))
                                         : QString();
        if (!normalizedFolderUuid.isEmpty() && existingUuid == normalizedFolderUuid)
        {
            return true;
        }
        if (normalizedFolderUuid.isEmpty()
            && normalizeFolderLookupKey(bindings.folders.at(index)) == normalizedFolderKey)
        {
            return true;
        }
    }

    return false;
}

bool ThinkingSpaceNoteFolderBindingService::matches(const Bindings& lhs, const Bindings& rhs) const
{
    if (lhs.folders.size() != rhs.folders.size())
    {
        return false;
    }

    for (int index = 0; index < lhs.folders.size(); ++index)
    {
        const QString lhsUuid = index < lhs.folderUuids.size()
                                    ? normalizeFolderUuid(lhs.folderUuids.at(index))
                                    : QString();
        const QString rhsUuid = index < rhs.folderUuids.size()
                                    ? normalizeFolderUuid(rhs.folderUuids.at(index))
                                    : QString();
        if (!lhsUuid.isEmpty() || !rhsUuid.isEmpty())
        {
            if (lhsUuid != rhsUuid)
            {
                return false;
            }
        }

        if (normalizeFolderLookupKey(lhs.folders.at(index)) != normalizeFolderLookupKey(rhs.folders.at(index)))
        {
            return false;
        }
    }

    return true;
}
