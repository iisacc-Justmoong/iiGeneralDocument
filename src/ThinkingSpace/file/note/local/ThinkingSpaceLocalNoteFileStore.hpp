#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteDocument.hpp"

#include <QJsonObject>
#include <QString>

class ThinkingSpaceLocalNoteFileStore final
{
public:
    struct ReadRequest final
    {
        QString noteId;
        QString noteDirectoryPath;
        QString noteHeaderPath;
        QString noteBodyPath;
    };

    struct CreateRequest final
    {
        QString noteId;
        QString noteDirectoryPath;
        ThinkingSpaceNoteHeaderStore headerStore;
        QString bodyPlainText;
    };

    struct UpdateRequest final
    {
        ThinkingSpaceLocalNoteDocument document;
        bool persistHeader = true;
        bool persistBody = true;
        bool touchLastModified = false;
        bool incrementModifiedCount = true;
        bool refreshIncomingBacklinkStatistics = true;
        bool refreshAffectedBacklinkTargets = true;
        bool resolveTimestampConflicts = true;
        QString baseLastModifiedAt;
        QString incomingLastModifiedAt;
    };

    struct UpdateResult final
    {
        bool versionDiffPushedToFilesystem = false;
        bool conflictResolvedByTimestamp = false;
        QString versionDiffFilePath;
        QString headerDiffGeneratedAtUtc;
        QString bodyDiffGeneratedAtUtc;
        QString conflictWinner;
        QString conflictBaseLastModifiedAt;
        QString conflictFilesystemLastModifiedAt;
        QString conflictIncomingLastModifiedAt;
    };

    struct DeleteRequest final
    {
        QString noteDirectoryPath;
        QString noteHeaderPath;
        QString noteBodyPath;
    };

    ThinkingSpaceLocalNoteFileStore();
    ~ThinkingSpaceLocalNoteFileStore();

    bool createNote(CreateRequest request, ThinkingSpaceLocalNoteDocument* outDocument = nullptr, QString* errorMessage = nullptr) const;
    bool readNote(ReadRequest request, ThinkingSpaceLocalNoteDocument* outDocument, QString* errorMessage = nullptr) const;
    bool updateNote(
        UpdateRequest request,
        ThinkingSpaceLocalNoteDocument* outDocument = nullptr,
        QString* errorMessage = nullptr,
        UpdateResult* outResult = nullptr) const;
    bool deleteNote(DeleteRequest request, QString* errorMessage = nullptr) const;

private:
    QString normalizePath(QString path) const;
    QString resolveNoteStem(const QString& noteId, const QString& noteDirectoryPath) const;
    QString resolveNoteId(const QString& noteId, const QString& noteDirectoryPath, const ThinkingSpaceNoteHeaderStore* headerStore = nullptr) const;
    QString resolveDirectoryPath(const QString& noteDirectoryPath, const QString& noteHeaderPath, const QString& noteBodyPath) const;
    QString headerPathForDirectory(const QString& noteId, const QString& noteDirectoryPath) const;
    QString bodyPathForDirectory(const QString& noteId, const QString& noteDirectoryPath) const;
    QString versionPathForDirectory(const QString& noteId, const QString& noteDirectoryPath) const;
    QString paintPathForDirectory(const QString& noteId, const QString& noteDirectoryPath) const;
    QString currentNoteTimestamp() const;

    bool loadHeaderStore(const QString& headerPath, ThinkingSpaceNoteHeaderStore* outHeaderStore, QString* errorMessage = nullptr) const;
    void applyBodyDocumentText(const QString& bodyDocumentText, ThinkingSpaceLocalNoteDocument* document) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
