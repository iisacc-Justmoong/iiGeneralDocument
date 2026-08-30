#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderStore.hpp"

#include <QString>
#include <QStringList>

namespace ThinkingSpace::NoteFileStatSupport
{
    QStringList extractBacklinkTargets(const QString& bodySourceText, const QString& bodyDocumentText);

    void applyBodyDerivedStatistics(
        ThinkingSpaceNoteHeaderStore* headerStore,
        const QString& bodySourceText,
        const QString& bodyDocumentText);

    bool applyTrackedStatistics(
        ThinkingSpaceNoteHeaderStore* headerStore,
        const QString& noteDirectoryPath,
        const QString& bodySourceText,
        const QString& bodyDocumentText,
        QString* errorMessage = nullptr);

    bool incrementOpenCountForNoteHeader(
        const QString& noteId,
        const QString& noteDirectoryPath,
        QString* errorMessage = nullptr);

    bool refreshTrackedStatisticsForNote(
        const QString& noteId,
        const QString& noteDirectoryPath,
        bool incrementOpenCount,
        QString* errorMessage = nullptr);

    bool refreshTrackedStatisticsForNoteId(
        const QString& noteId,
        const QString& referenceNoteDirectoryPath,
        QString* errorMessage = nullptr);
} // namespace ThinkingSpace::NoteFileStatSupport

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
