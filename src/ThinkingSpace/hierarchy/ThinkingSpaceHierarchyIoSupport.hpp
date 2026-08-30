#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"
#include "ThinkingSpace/file/hub/ThinkingSpaceHubPathUtils.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QVector>

namespace ThinkingSpace::Hierarchy::IoSupport
{
    // Shared .tshub/.tscontents discovery and UTF-8 file IO for hierarchy domains.
    inline QString normalizePath(const QString& input)
    {
        return ThinkingSpace::HubPath::normalizePath(input);
    }

    inline bool resolveContentsDirectories(
        const QString& tshubPath,
        QStringList* outContentsDirectories,
        QString* errorMessage = nullptr)
    {
        if (outContentsDirectories == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("outContentsDirectories must not be null.");
            }
            return false;
        }

        outContentsDirectories->clear();

        const QString hubRootPath = normalizePath(tshubPath);
        if (hubRootPath.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("tshubPath must not be empty.");
            }
            return false;
        }

        const QFileInfo hubInfo(hubRootPath);
        if (!hubInfo.exists())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("tshubPath does not exist: %1").arg(hubRootPath);
            }
            return false;
        }

        if (!hubInfo.fileName().endsWith(QStringLiteral(".tshub")) || !hubInfo.isDir())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("tshubPath must be an unpacked .tshub directory: %1").arg(hubRootPath);
            }
            return false;
        }

        const QDir hubDir(hubRootPath);
        const QString fixedInternalPath = hubDir.filePath(QStringLiteral(".tscontents"));
        if (QFileInfo(fixedInternalPath).isDir())
        {
            outContentsDirectories->push_back(ThinkingSpace::HubPath::normalizePath(fixedInternalPath));
        }

        const QStringList dynamicContentsDirectories = hubDir.entryList(
            QStringList{QStringLiteral("*.tscontents")},
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QString& directoryName : dynamicContentsDirectories)
        {
            outContentsDirectories->push_back(ThinkingSpace::HubPath::joinPath(hubDir.path(), directoryName));
        }

        outContentsDirectories->removeDuplicates();
        if (!outContentsDirectories->isEmpty())
        {
            return true;
        }

        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("No *.tscontents directory was found inside .tshub: %1").arg(hubRootPath);
        }
        return false;
    }

    inline bool readUtf8File(const QString& filePath, QString* outText, QString* errorMessage = nullptr)
    {
        ThinkingSpace::Debug::trace(
            QStringLiteral("hierarchy.io.support"),
            QStringLiteral("readUtf8File.begin"),
            QStringLiteral("path=%1").arg(filePath));

        if (outText == nullptr)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("outText must not be null.");
            }
            ThinkingSpace::Debug::trace(
                QStringLiteral("hierarchy.io.support"),
                QStringLiteral("readUtf8File.failed"),
                QStringLiteral("path=%1 reason=outText is null").arg(filePath));
            return false;
        }

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to open file: %1").arg(filePath);
            }
            ThinkingSpace::Debug::trace(
                QStringLiteral("hierarchy.io.support"),
                QStringLiteral("readUtf8File.failed"),
                QStringLiteral("path=%1 reason=open failed").arg(filePath));
            return false;
        }

        const QByteArray bytes = file.readAll();
        *outText = QString::fromUtf8(bytes);
        ThinkingSpace::Debug::trace(
            QStringLiteral("hierarchy.io.support"),
            QStringLiteral("readUtf8File.success"),
            QStringLiteral("path=%1 bytes=%2").arg(filePath).arg(bytes.size()));
        return true;
    }

    template <typename Transform>
    inline QStringList deduplicateStringsPreservingOrder(QStringList values, Transform transform)
    {
        QStringList deduplicated;
        deduplicated.reserve(values.size());

        QSet<QString> seen;
        seen.reserve(values.size());

        for (QString& value : values)
        {
            const QString normalized = transform(value);
            if (normalized.isEmpty() || seen.contains(normalized))
            {
                continue;
            }

            seen.insert(normalized);
            deduplicated.push_back(normalized);
        }

        return deduplicated;
    }

    template <typename Item, typename SkipPredicate>
    inline QStringList extractDistinctLabelsFromItems(
        const QVector<Item>& items,
        SkipPredicate shouldSkip)
    {
        QStringList labels;
        labels.reserve(items.size());

        QSet<QString> seen;
        seen.reserve(items.size());

        for (int index = 0; index < items.size(); ++index)
        {
            const Item& item = items.at(index);
            if (shouldSkip(index, item))
            {
                continue;
            }

            const QString label = item.label.trimmed();
            if (label.isEmpty() || seen.contains(label))
            {
                continue;
            }

            seen.insert(label);
            labels.push_back(label);
        }

        return labels;
    }
} // namespace ThinkingSpace::Hierarchy::IoSupport

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
