#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteFileStore.hpp"
#include "ThinkingSpace/hierarchy/library/LibraryNoteRecord.hpp"
#include "ThinkingSpace/file/hub/ThinkingSpaceHubStat.hpp"
#include "ThinkingSpace/file/validator/ThinkingSpaceHubStructureValidator.hpp"
#include "ThinkingSpace/file/validator/ThinkingSpaceLibraryIndexIntegrityValidator.hpp"
#include "ThinkingSpace/file/validator/ThinkingSpaceNoteStorageValidator.hpp"

#include <QString>
#include <QVector>

class ThinkingSpaceHubNoteDeletionService final
{
public:
    struct Request final
    {
        QString tshubPath;
        QString libraryPath;
        QString statPath;
        QString hubName;
        ThinkingSpaceHubStat hubStat;
        QVector<LibraryNoteRecord> notes;
        QString noteId;
    };

    struct Result final
    {
        QString noteId;
        QString tshubPath;
        QString libraryPath;
        QString statPath;
        ThinkingSpaceHubStat hubStat;
        QVector<LibraryNoteRecord> remainingNotes;
    };

    ThinkingSpaceHubNoteDeletionService();
    ~ThinkingSpaceHubNoteDeletionService();

    bool deleteNote(Request request, Result* outResult = nullptr, QString* errorMessage = nullptr) const;

private:
    ThinkingSpaceLocalNoteFileStore m_localNoteFileStore;
    ThinkingSpaceHubStructureValidator m_hubStructureValidator;
    ThinkingSpaceLibraryIndexIntegrityValidator m_libraryIndexIntegrityValidator;
    ThinkingSpaceNoteStorageValidator m_noteStorageValidator;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
