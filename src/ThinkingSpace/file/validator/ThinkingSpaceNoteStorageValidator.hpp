#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/hierarchy/library/LibraryNoteRecord.hpp"

#include <QString>

class ThinkingSpaceNoteStorageValidator final
{
public:
    ThinkingSpaceNoteStorageValidator();
    ~ThinkingSpaceNoteStorageValidator();

    QString resolveExistingNoteHeaderPath(const LibraryNoteRecord& record) const;
    QString resolveExistingNoteDirectoryPath(const LibraryNoteRecord& record) const;
    bool hasMaterializedStorage(const LibraryNoteRecord& record) const;
    bool normalizeWsnotePackage(const LibraryNoteRecord& record, QString* errorMessage = nullptr) const;

private:
    QString normalizePath(const QString& path) const;
    bool normalizeWsnotePackageByDirectoryPath(
        const QString& noteDirectoryPath,
        const QString& noteId,
        QString* errorMessage = nullptr) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
