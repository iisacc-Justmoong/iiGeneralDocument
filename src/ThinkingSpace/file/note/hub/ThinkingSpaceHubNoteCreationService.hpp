#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/hub/ThinkingSpaceHubNoteMutationSupport.hpp"
#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteFileStore.hpp"
#include "ThinkingSpace/file/hub/ThinkingSpaceHubStat.hpp"
#include "ThinkingSpace/hierarchy/library/LibraryNoteRecord.hpp"
#include "ThinkingSpace/file/validator/ThinkingSpaceHubStructureValidator.hpp"
#include "ThinkingSpace/file/validator/ThinkingSpaceLibraryIndexIntegrityValidator.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

class ThinkingSpaceHubNoteCreationService final
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
        QString authorProfileName;
        QStringList assignedFolders;
        QStringList assignedFolderUuids;
    };

    struct Result final
    {
        QString noteId;
        QString tshubPath;
        QString libraryPath;
        QString statPath;
        ThinkingSpaceHubStat hubStat;
        QVector<LibraryNoteRecord> notes;
    };

    ThinkingSpaceHubNoteCreationService();
    ~ThinkingSpaceHubNoteCreationService();

    bool createNote(Request request, Result* outResult = nullptr, QString* errorMessage = nullptr) const;

private:
    ThinkingSpaceLocalNoteFileStore m_localNoteFileStore;
    ThinkingSpaceHubStructureValidator m_hubStructureValidator;
    ThinkingSpaceLibraryIndexIntegrityValidator m_libraryIndexIntegrityValidator;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
