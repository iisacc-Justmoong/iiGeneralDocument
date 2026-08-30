#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/hub/ThinkingSpaceHubNoteMutationSupport.hpp"
#include "ThinkingSpace/file/note/folder/ThinkingSpaceNoteFolderBindingRepository.hpp"
#include "ThinkingSpace/hierarchy/library/LibraryNoteRecord.hpp"

#include <QString>
#include <QVector>

class ThinkingSpaceHubNoteFolderClearService final
{
public:
    struct Request final
    {
        QVector<LibraryNoteRecord> notes;
        QString noteId;
    };

    struct Result final
    {
        QString noteId;
        bool foldersCleared = false;
        QVector<LibraryNoteRecord> notes;
    };

    ThinkingSpaceHubNoteFolderClearService();
    ~ThinkingSpaceHubNoteFolderClearService();

    bool clearFolders(Request request, Result* outResult = nullptr, QString* errorMessage = nullptr) const;

private:
    ThinkingSpaceNoteFolderBindingRepository m_noteFolderBindingRepository;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
