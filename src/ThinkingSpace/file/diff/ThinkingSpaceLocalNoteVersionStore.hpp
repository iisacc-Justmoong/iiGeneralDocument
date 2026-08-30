#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteDocument.hpp"

#include <QString>
#include <QVector>

struct ThinkingSpaceNoteVersionDiffSegment final
{
    int prefixLength = 0;
    int suffixLength = 0;
    QString removedText;
    QString insertedText;
    QString unifiedPatch;
    QString generatedAtUtc;
};

struct ThinkingSpaceNoteVersionSnapshot final
{
    QString snapshotId;
    QString parentSnapshotId;
    QString sourceSnapshotId;
    QString label;
    QString createdAtUtc;
    int commitModifiedCount = 0;
    QString headerText;
    QString bodyDocumentText;
    QString bodyPlainText;
    ThinkingSpaceNoteVersionDiffSegment headerDiff;
    ThinkingSpaceNoteVersionDiffSegment bodyDiff;
};

struct ThinkingSpaceNoteVersionState final
{
    QString noteId;
    QString currentSnapshotId;
    QString headSnapshotId;
    QVector<ThinkingSpaceNoteVersionSnapshot> snapshots;
};

struct ThinkingSpaceNoteVersionDiffResult final
{
    ThinkingSpaceNoteVersionSnapshot baseSnapshot;
    ThinkingSpaceNoteVersionSnapshot targetSnapshot;
    ThinkingSpaceNoteVersionDiffSegment headerDiff;
    ThinkingSpaceNoteVersionDiffSegment bodyDiff;
};

class ThinkingSpaceLocalNoteVersionStore final
{
public:
    struct CaptureRequest final
    {
        ThinkingSpaceLocalNoteDocument document;
        QString label;
        int commitModifiedCount = 0;
    };

    struct DiffRequest final
    {
        QString versionFilePath;
        QString baseSnapshotId;
        QString targetSnapshotId;
    };

    struct CheckoutRequest final
    {
        QString versionFilePath;
        QString snapshotId;
        QString noteHeaderPath;
        QString noteBodyPath;
    };

    struct RollbackRequest final
    {
        QString versionFilePath;
        QString snapshotId;
        QString noteHeaderPath;
        QString noteBodyPath;
        QString label;
        int commitModifiedCount = 0;
    };

    bool loadState(
        const QString& versionFilePath,
        ThinkingSpaceNoteVersionState* outState,
        QString* errorMessage = nullptr) const;

    bool captureSnapshot(
        CaptureRequest request,
        ThinkingSpaceNoteVersionSnapshot* outSnapshot = nullptr,
        ThinkingSpaceNoteVersionState* outState = nullptr,
        QString* errorMessage = nullptr) const;

    bool diffSnapshots(
        DiffRequest request,
        ThinkingSpaceNoteVersionDiffResult* outResult,
        QString* errorMessage = nullptr) const;

    bool checkoutSnapshot(
        CheckoutRequest request,
        QString* errorMessage = nullptr) const;

    bool rollbackToSnapshot(
        RollbackRequest request,
        ThinkingSpaceNoteVersionSnapshot* outSnapshot = nullptr,
        QString* errorMessage = nullptr) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
