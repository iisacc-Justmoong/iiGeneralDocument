#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderStore.hpp"
#include "ThinkingSpace/hierarchy/library/LibraryNoteRecord.hpp"

#include <QString>

struct ThinkingSpaceLocalNoteDocument
{
    QString noteDirectoryPath;
    QString noteHeaderPath;
    QString noteBodyPath;
    QString noteVersionPath;
    QString notePaintPath;
    ThinkingSpaceNoteHeaderStore headerStore;
    QString bodyPlainText;
    QString bodySourceText;
    QString bodyFirstLine;
    bool bodyHasResource = false;
    QString bodyFirstResourceThumbnailUrl;

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
                ThinkingSpace::NoteBodyPersistence::serializeBodyDocument(headerStore.noteId(), normalizedSourceText));
        }
        else
        {
            normalizedSourceText = normalizedPlainText;
        }
        bodyPlainText = normalizedPlainText;
        bodySourceText = normalizedSourceText;
        bodyFirstLine = ThinkingSpace::NoteBodyPersistence::firstLineFromBodyPlainText(normalizedPlainText);
    }

    LibraryNoteRecord toLibraryNoteRecord() const
    {
        LibraryNoteRecord record;
        record.noteId = headerStore.noteId();
        record.storageKind = QStringLiteral("tsnote");
        record.bodyPlainText = bodyPlainText;
        record.bodySourceText = bodySourceText;
        record.bodyFirstLine = bodyFirstLine;
        record.normalizeBodyFields();
        record.bodyHasResource = bodyHasResource;
        record.bodyFirstResourceThumbnailUrl = bodyFirstResourceThumbnailUrl.trimmed();
        record.createdAt = headerStore.createdAt();
        record.lastModifiedAt = headerStore.lastModifiedAt();
        record.author = headerStore.author();
        record.modifiedBy = headerStore.modifiedBy();
        record.project = headerStore.project();
        record.folders = headerStore.folders();
        record.folderUuids = headerStore.folderUuids();
        record.bookmarkColors = headerStore.bookmarkColors();
        record.tags = headerStore.tags();
        record.progress = headerStore.progress();
        record.bookmarked = headerStore.isBookmarked();
        record.preset = headerStore.isPreset();
        record.noteDirectoryPath = noteDirectoryPath.trimmed();
        record.noteHeaderPath = noteHeaderPath.trimmed();
        return record;
    }
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
