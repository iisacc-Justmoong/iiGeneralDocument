#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteDocument.hpp"
#include "ThinkingSpace/hierarchy/library/LibraryNoteRecord.hpp"

#include <QString>
#include <QVector>

namespace ThinkingSpace::NoteMutationSupport
{
    QString currentNoteTimestamp();
    int indexOfNoteRecordById(const QVector<LibraryNoteRecord>& notes, const QString& noteId);
    QString createUniqueNoteId(const QString& libraryPath, const QVector<LibraryNoteRecord>& existingNotes);
    bool ensureDirectoryPath(const QString& directoryPath, QString* errorMessage = nullptr);
    bool readUtf8File(const QString& filePath, QString* outText, QString* errorMessage = nullptr);
    bool writeUtf8File(const QString& filePath, const QString& text, QString* errorMessage = nullptr);
    bool removeFilePath(const QString& filePath, QString* errorMessage = nullptr);
    bool removeDirectoryPath(const QString& directoryPath, QString* errorMessage = nullptr);
    bool pathExists(const QString& path);
    QString resolveNoteHeaderPath(const LibraryNoteRecord& note);
    void syncNoteRecordFromDocument(LibraryNoteRecord* note, const ThinkingSpaceLocalNoteDocument& document);
} // namespace ThinkingSpace::NoteMutationSupport

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
