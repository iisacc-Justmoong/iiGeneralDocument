#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"

struct LibraryNoteRecord
{
    QString noteId;
    QString storageKind;
    QString bodyPlainText;
    QString bodySourceText;
    QString bodyFirstLine;
    bool bodyHasResource = false;
    QString bodyFirstResourceThumbnailUrl;
    QString createdAt;
    QString lastModifiedAt;
    QString author;
    QString modifiedBy;
    QString project;
    QStringList folders;
    QStringList folderUuids;
    QStringList bookmarkColors;
    QStringList tags;
    int progress = -1;
    bool bookmarked = false;
    bool preset = false;
    QString noteDirectoryPath;
    QString noteHeaderPath;

    QString effectiveBodyText() const
    {
        const QString normalizedSource = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodySourceText);
        if (!normalizedSource.isEmpty())
        {
            return normalizedSource;
        }
        return ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodyPlainText);
    }

    void normalizeBodyFields()
    {
        QString normalizedSourceText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodySourceText);
        QString normalizedPlainText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodyPlainText);
        if (!normalizedSourceText.isEmpty())
        {
            normalizedPlainText = ThinkingSpace::NoteBodyPersistence::plainTextFromBodyDocument(
                ThinkingSpace::NoteBodyPersistence::serializeBodyDocument(noteId, normalizedSourceText));
        }
        else
        {
            normalizedSourceText = normalizedPlainText;
        }
        bodyPlainText = normalizedPlainText;
        bodySourceText = normalizedSourceText;
        bodyFirstLine = ThinkingSpace::NoteBodyPersistence::firstLineFromBodyPlainText(normalizedPlainText);
    }

    bool operator==(const LibraryNoteRecord& other) const
    {
        return noteId == other.noteId
            && storageKind == other.storageKind
            && bodyPlainText == other.bodyPlainText
            && bodySourceText == other.bodySourceText
            && bodyFirstLine == other.bodyFirstLine
            && bodyHasResource == other.bodyHasResource
            && bodyFirstResourceThumbnailUrl == other.bodyFirstResourceThumbnailUrl
            && createdAt == other.createdAt
            && lastModifiedAt == other.lastModifiedAt
            && author == other.author
            && modifiedBy == other.modifiedBy
            && project == other.project
            && folders == other.folders
            && folderUuids == other.folderUuids
            && bookmarkColors == other.bookmarkColors
            && tags == other.tags
            && progress == other.progress
            && bookmarked == other.bookmarked
            && preset == other.preset
            && noteDirectoryPath == other.noteDirectoryPath
            && noteHeaderPath == other.noteHeaderPath;
    }

    bool operator!=(const LibraryNoteRecord& other) const
    {
        return !(*this == other);
    }
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
