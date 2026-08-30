#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteDocument.hpp"
#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteFileStore.hpp"
#include "ThinkingSpace/file/note/folder/ThinkingSpaceNoteFolderBindingService.hpp"
#include "ThinkingSpace/hierarchy/library/LibraryNoteRecord.hpp"

#include <QString>

class ThinkingSpaceNoteFolderBindingRepository final
{
public:
    ThinkingSpaceNoteFolderBindingRepository();
    ~ThinkingSpaceNoteFolderBindingRepository();

    bool readDocument(const LibraryNoteRecord& note, ThinkingSpaceLocalNoteDocument* outDocument, QString* errorMessage = nullptr) const;
    bool readDocument(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const QString& noteHeaderPath,
        ThinkingSpaceLocalNoteDocument* outDocument,
        QString* errorMessage = nullptr) const;
    bool writeDocument(
        ThinkingSpaceLocalNoteDocument document,
        ThinkingSpaceLocalNoteDocument* outDocument = nullptr,
        QString* errorMessage = nullptr) const;
    bool writeFolderBindings(
        ThinkingSpaceLocalNoteDocument document,
        const ThinkingSpaceNoteFolderBindingService::Bindings& bindings,
        ThinkingSpaceLocalNoteDocument* outDocument = nullptr,
        QString* errorMessage = nullptr) const;

private:
    ThinkingSpaceLocalNoteFileStore m_localNoteFileStore;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
