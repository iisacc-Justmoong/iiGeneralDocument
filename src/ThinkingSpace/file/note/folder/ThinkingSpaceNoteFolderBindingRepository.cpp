#include "ThinkingSpace/file/note/folder/ThinkingSpaceNoteFolderBindingRepository.hpp"

#include "ThinkingSpace/file/note/hub/ThinkingSpaceHubNoteMutationSupport.hpp"

#include <utility>

ThinkingSpaceNoteFolderBindingRepository::ThinkingSpaceNoteFolderBindingRepository() = default;

ThinkingSpaceNoteFolderBindingRepository::~ThinkingSpaceNoteFolderBindingRepository() = default;

bool ThinkingSpaceNoteFolderBindingRepository::readDocument(
    const LibraryNoteRecord& note,
    ThinkingSpaceLocalNoteDocument* outDocument,
    QString* errorMessage) const
{
    return readDocument(
        note.noteId,
        note.noteDirectoryPath,
        ThinkingSpace::NoteMutationSupport::resolveNoteHeaderPath(note),
        outDocument,
        errorMessage);
}

bool ThinkingSpaceNoteFolderBindingRepository::readDocument(
    const QString& noteId,
    const QString& noteDirectoryPath,
    const QString& noteHeaderPath,
    ThinkingSpaceLocalNoteDocument* outDocument,
    QString* errorMessage) const
{
    if (outDocument == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("outDocument must not be null.");
        }
        return false;
    }

    const QString normalizedNoteId = noteId.trimmed();
    const QString normalizedHeaderPath = noteHeaderPath.trimmed();
    if (normalizedNoteId.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("noteId must not be empty.");
        }
        return false;
    }
    if (normalizedHeaderPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("noteHeaderPath must not be empty.");
        }
        return false;
    }

    ThinkingSpaceLocalNoteFileStore::ReadRequest readRequest;
    readRequest.noteId = normalizedNoteId;
    readRequest.noteDirectoryPath = noteDirectoryPath;
    readRequest.noteHeaderPath = normalizedHeaderPath;
    return m_localNoteFileStore.readNote(std::move(readRequest), outDocument, errorMessage);
}

bool ThinkingSpaceNoteFolderBindingRepository::writeDocument(
    ThinkingSpaceLocalNoteDocument document,
    ThinkingSpaceLocalNoteDocument* outDocument,
    QString* errorMessage) const
{
    ThinkingSpaceLocalNoteFileStore::UpdateRequest updateRequest;
    updateRequest.document = std::move(document);
    updateRequest.persistHeader = true;
    updateRequest.persistBody = false;
    return m_localNoteFileStore.updateNote(std::move(updateRequest), outDocument, errorMessage);
}

bool ThinkingSpaceNoteFolderBindingRepository::writeFolderBindings(
    ThinkingSpaceLocalNoteDocument document,
    const ThinkingSpaceNoteFolderBindingService::Bindings& bindings,
    ThinkingSpaceLocalNoteDocument* outDocument,
    QString* errorMessage) const
{
    document.headerStore.setFolderBindings(bindings.folders, bindings.folderUuids);
    return writeDocument(std::move(document), outDocument, errorMessage);
}
