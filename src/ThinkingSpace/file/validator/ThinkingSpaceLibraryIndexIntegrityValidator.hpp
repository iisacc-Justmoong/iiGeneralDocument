#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/validator/ThinkingSpaceNoteStorageValidator.hpp"
#include "ThinkingSpace/hierarchy/library/LibraryNoteRecord.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

class ThinkingSpaceLibraryIndexIntegrityValidator final
{
public:
    struct PruneResult final
    {
        QVector<LibraryNoteRecord> materializedRecords;
        QStringList prunedOrphanNoteIds;
    };

    ThinkingSpaceLibraryIndexIntegrityValidator();
    ~ThinkingSpaceLibraryIndexIntegrityValidator();

    PruneResult pruneOrphanRecords(const QVector<LibraryNoteRecord>& records) const;
    bool rewriteIndexesFromRecords(
        const QString& sourceWshubPath,
        const QStringList& libraryRoots,
        const QVector<LibraryNoteRecord>& records,
        QString* errorMessage = nullptr) const;

private:
    QString normalizePath(const QString& path) const;
    bool pathIsWithinRoot(const QString& path, const QString& rootPath) const;
    bool recordBelongsToLibraryRoot(const LibraryNoteRecord& record, const QString& libraryRoot) const;
    QStringList noteIdsForLibraryRoot(const QVector<LibraryNoteRecord>& records, const QString& libraryRoot) const;
    ThinkingSpaceNoteStorageValidator m_noteStorageValidator;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
