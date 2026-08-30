#include "ThinkingSpace/file/validator/ThinkingSpaceLibraryIndexIntegrityValidator.hpp"

#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"
#include "ThinkingSpace/file/note/hub/ThinkingSpaceHubNoteMutationSupport.hpp"
#include "ThinkingSpace/hierarchy/library/ThinkingSpaceLibraryHierarchyCreator.hpp"
#include "ThinkingSpace/hierarchy/library/ThinkingSpaceLibraryHierarchyStore.hpp"

#include <QDir>
#include <QFileInfo>

#include <utility>

ThinkingSpaceLibraryIndexIntegrityValidator::ThinkingSpaceLibraryIndexIntegrityValidator() = default;

ThinkingSpaceLibraryIndexIntegrityValidator::~ThinkingSpaceLibraryIndexIntegrityValidator() = default;

QString ThinkingSpaceLibraryIndexIntegrityValidator::normalizePath(const QString& path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    return QDir::cleanPath(trimmed);
}

bool ThinkingSpaceLibraryIndexIntegrityValidator::pathIsWithinRoot(const QString& path, const QString& rootPath) const
{
    const QString normalizedPath = normalizePath(path);
    const QString normalizedRootPath = normalizePath(rootPath);
    if (normalizedPath.isEmpty() || normalizedRootPath.isEmpty())
    {
        return false;
    }
    if (normalizedPath == normalizedRootPath)
    {
        return true;
    }

    return normalizedPath.startsWith(normalizedRootPath + QLatin1Char('/'), Qt::CaseInsensitive);
}

bool ThinkingSpaceLibraryIndexIntegrityValidator::recordBelongsToLibraryRoot(
    const LibraryNoteRecord& record,
    const QString& libraryRoot) const
{
    const QString directoryPath = m_noteStorageValidator.resolveExistingNoteDirectoryPath(record);
    if (pathIsWithinRoot(directoryPath, libraryRoot))
    {
        return true;
    }

    const QString headerPath = m_noteStorageValidator.resolveExistingNoteHeaderPath(record);
    return pathIsWithinRoot(headerPath, libraryRoot);
}

QStringList ThinkingSpaceLibraryIndexIntegrityValidator::noteIdsForLibraryRoot(
    const QVector<LibraryNoteRecord>& records,
    const QString& libraryRoot) const
{
    QStringList noteIds;
    noteIds.reserve(records.size());

    for (const LibraryNoteRecord& record : records)
    {
        if (!recordBelongsToLibraryRoot(record, libraryRoot))
        {
            continue;
        }

        const QString noteId = record.noteId.trimmed();
        if (!noteId.isEmpty())
        {
            noteIds.push_back(noteId);
        }
    }

    noteIds.removeDuplicates();
    return noteIds;
}

ThinkingSpaceLibraryIndexIntegrityValidator::PruneResult
ThinkingSpaceLibraryIndexIntegrityValidator::pruneOrphanRecords(const QVector<LibraryNoteRecord>& records) const
{
    PruneResult result;
    result.materializedRecords.reserve(records.size());

    for (LibraryNoteRecord record : records)
    {
        QString normalizationError;
        if (!m_noteStorageValidator.normalizeWsnotePackage(record, &normalizationError))
        {
            ThinkingSpace::Debug::trace(
                QStringLiteral("library.validator"),
                QStringLiteral("notePackage.normalizeFailed"),
                QStringLiteral("noteId=%1 dir=%2 reason=%3")
                    .arg(record.noteId, record.noteDirectoryPath, normalizationError));
        }

        if (!m_noteStorageValidator.hasMaterializedStorage(record))
        {
            const QString orphanNoteId = record.noteId.trimmed();
            if (!orphanNoteId.isEmpty())
            {
                result.prunedOrphanNoteIds.push_back(orphanNoteId);
            }

            ThinkingSpace::Debug::trace(
                QStringLiteral("library.validator"),
                QStringLiteral("index.pruneOrphanRecord"),
                QStringLiteral("noteId=%1 head=%2 dir=%3")
                .arg(record.noteId, record.noteHeaderPath, record.noteDirectoryPath));
            continue;
        }

        result.materializedRecords.push_back(std::move(record));
    }

    return result;
}

bool ThinkingSpaceLibraryIndexIntegrityValidator::rewriteIndexesFromRecords(
    const QString& sourceWshubPath,
    const QStringList& libraryRoots,
    const QVector<LibraryNoteRecord>& records,
    QString* errorMessage) const
{
    for (const QString& libraryRoot : libraryRoots)
    {
        const QString indexPath = QDir(libraryRoot).filePath(QStringLiteral("index.tsnindex"));

        ThinkingSpaceLibraryHierarchyStore libraryStore;
        libraryStore.setHubPath(sourceWshubPath);
        libraryStore.setNoteIds(noteIdsForLibraryRoot(records, libraryRoot));

        ThinkingSpaceLibraryHierarchyCreator libraryCreator;
        QString writeError;
        if (!ThinkingSpace::NoteMutationSupport::writeUtf8File(indexPath, libraryCreator.createText(libraryStore), &writeError))
        {
            ThinkingSpace::Debug::trace(
                QStringLiteral("library.validator"),
                QStringLiteral("index.rewriteFailed"),
                QStringLiteral("path=%1 reason=%2").arg(indexPath, writeError));
            if (errorMessage != nullptr)
            {
                *errorMessage = writeError;
            }
            return false;
        }

        ThinkingSpace::Debug::trace(
            QStringLiteral("library.validator"),
            QStringLiteral("index.rewrite"),
            QStringLiteral("path=%1 noteCount=%2")
            .arg(indexPath)
            .arg(libraryStore.noteIds().size()));
    }

    return true;
}
