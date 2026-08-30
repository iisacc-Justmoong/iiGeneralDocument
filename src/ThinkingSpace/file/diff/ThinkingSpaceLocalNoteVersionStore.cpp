#include "ThinkingSpace/file/diff/ThinkingSpaceLocalNoteVersionStore.hpp"

#include "ThinkingSpace/file/diff/VersionLimitManager.h"
#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionDiffBuilder.hpp"
#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionFileGateway.hpp"
#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionSnapshotBuilder.hpp"
#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionStateCodec.hpp"
#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"

#include <QDir>

#include <utility>

namespace
{
    bool readState(
        const ThinkingSpaceNoteVersionFileGateway& fileGateway,
        const ThinkingSpaceNoteVersionStateCodec& stateCodec,
        const QString& versionFilePath,
        ThinkingSpaceNoteVersionState* outState,
        QString* errorMessage)
    {
        QString versionText;
        if (!fileGateway.readUtf8File(versionFilePath, &versionText, errorMessage))
        {
            return false;
        }
        return stateCodec.parseState(versionText, versionFilePath, outState, errorMessage);
    }

    bool writeState(
        const ThinkingSpaceNoteVersionFileGateway& fileGateway,
        const ThinkingSpaceNoteVersionStateCodec& stateCodec,
        const QString& versionFilePath,
        const ThinkingSpaceNoteVersionState& state,
        QString* errorMessage)
    {
        return fileGateway.writeUtf8File(versionFilePath, stateCodec.serializeState(state), errorMessage);
    }
} // namespace

bool ThinkingSpaceLocalNoteVersionStore::loadState(
    const QString& versionFilePath,
    ThinkingSpaceNoteVersionState* outState,
    QString* errorMessage) const
{
    const ThinkingSpaceNoteVersionFileGateway fileGateway;
    const ThinkingSpaceNoteVersionStateCodec stateCodec;
    return readState(
        fileGateway,
        stateCodec,
        QDir::cleanPath(versionFilePath.trimmed()),
        outState,
        errorMessage);
}

bool ThinkingSpaceLocalNoteVersionStore::captureSnapshot(
    CaptureRequest request,
    ThinkingSpaceNoteVersionSnapshot* outSnapshot,
    ThinkingSpaceNoteVersionState* outState,
    QString* errorMessage) const
{
    const ThinkingSpaceNoteVersionFileGateway fileGateway;
    const ThinkingSpaceNoteVersionStateCodec stateCodec;
    const ThinkingSpaceNoteVersionSnapshotBuilder snapshotBuilder;

    const QString noteId = fileGateway.noteIdFromDocument(request.document);
    const QString versionFilePath = fileGateway.versionPathFromDocument(request.document);
    if (!fileGateway.ensureVersionDocument(
            versionFilePath,
            stateCodec.emptyStateText(noteId),
            errorMessage))
    {
        return false;
    }

    ThinkingSpaceNoteVersionState state;
    if (!readState(fileGateway, stateCodec, versionFilePath, &state, errorMessage))
    {
        return false;
    }
    if (state.noteId.trimmed().isEmpty())
    {
        state.noteId = noteId;
    }

    QString headerText;
    if (!fileGateway.readUtf8File(fileGateway.headerPathFromDocument(request.document), &headerText, errorMessage))
    {
        return false;
    }

    QString bodyDocumentText;
    if (!fileGateway.readUtf8File(fileGateway.bodyPathFromDocument(request.document), &bodyDocumentText, errorMessage))
    {
        return false;
    }

    QString bodyPlainText = request.document.bodyPlainText.trimmed().isEmpty()
                                ? ThinkingSpace::NoteBodyPersistence::plainTextFromBodyDocument(bodyDocumentText)
                                : request.document.bodyPlainText;
    bodyPlainText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodyPlainText);
    const QString label = request.label.trimmed().isEmpty()
                              ? QStringLiteral("commit:%1").arg(request.commitModifiedCount)
                              : request.label.trimmed();
    const QString parentSnapshotId = snapshotBuilder.parentSnapshotIdForCapture(state);
    ThinkingSpaceNoteVersionSnapshot snapshot = snapshotBuilder.buildSnapshot(
        state,
        parentSnapshotId,
        QString(),
        label,
        request.commitModifiedCount,
        headerText,
        bodyDocumentText,
        bodyPlainText);

    state.snapshots.push_back(snapshot);
    state.currentSnapshotId = snapshot.snapshotId;
    state.headSnapshotId = snapshot.snapshotId;
    VersionLimitManager::pruneOldestSnapshots(&state);

    if (!writeState(fileGateway, stateCodec, versionFilePath, state, errorMessage))
    {
        return false;
    }

    if (outSnapshot != nullptr)
    {
        *outSnapshot = snapshot;
    }
    if (outState != nullptr)
    {
        *outState = std::move(state);
    }
    return true;
}

bool ThinkingSpaceLocalNoteVersionStore::diffSnapshots(
    DiffRequest request,
    ThinkingSpaceNoteVersionDiffResult* outResult,
    QString* errorMessage) const
{
    if (outResult == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("outResult must not be null.");
        }
        return false;
    }

    const ThinkingSpaceNoteVersionFileGateway fileGateway;
    const ThinkingSpaceNoteVersionStateCodec stateCodec;
    const ThinkingSpaceNoteVersionSnapshotBuilder snapshotBuilder;
    const ThinkingSpaceNoteVersionDiffBuilder diffBuilder;

    ThinkingSpaceNoteVersionState state;
    if (!readState(
            fileGateway,
            stateCodec,
            QDir::cleanPath(request.versionFilePath.trimmed()),
            &state,
            errorMessage))
    {
        return false;
    }

    const ThinkingSpaceNoteVersionSnapshot* baseSnapshot =
        snapshotBuilder.findSnapshot(state, request.baseSnapshotId);
    const ThinkingSpaceNoteVersionSnapshot* targetSnapshot =
        snapshotBuilder.findSnapshot(state, request.targetSnapshotId);
    if (baseSnapshot == nullptr || targetSnapshot == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to resolve requested snapshots.");
        }
        return false;
    }

    ThinkingSpaceNoteVersionDiffResult result;
    result.baseSnapshot = *baseSnapshot;
    result.targetSnapshot = *targetSnapshot;
    result.headerDiff = diffBuilder.diffSegment(
        baseSnapshot->headerText,
        targetSnapshot->headerText,
        QStringLiteral("header.tsnhead"));
    result.bodyDiff = diffBuilder.diffSegment(
        baseSnapshot->bodyDocumentText,
        targetSnapshot->bodyDocumentText,
        QStringLiteral("body.tsnbody"));
    *outResult = std::move(result);
    return true;
}

bool ThinkingSpaceLocalNoteVersionStore::checkoutSnapshot(
    CheckoutRequest request,
    QString* errorMessage) const
{
    const ThinkingSpaceNoteVersionFileGateway fileGateway;
    const ThinkingSpaceNoteVersionStateCodec stateCodec;
    const ThinkingSpaceNoteVersionSnapshotBuilder snapshotBuilder;
    const QString versionFilePath = QDir::cleanPath(request.versionFilePath.trimmed());

    ThinkingSpaceNoteVersionState state;
    if (!readState(fileGateway, stateCodec, versionFilePath, &state, errorMessage))
    {
        return false;
    }

    const ThinkingSpaceNoteVersionSnapshot* snapshot = snapshotBuilder.findSnapshot(state, request.snapshotId);
    if (snapshot == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to resolve checkout snapshot: %1").arg(request.snapshotId);
        }
        return false;
    }

    if (!fileGateway.writeUtf8File(
            QDir::cleanPath(request.noteHeaderPath.trimmed()),
            snapshot->headerText,
            errorMessage)
        || !fileGateway.writeUtf8File(
            QDir::cleanPath(request.noteBodyPath.trimmed()),
            snapshot->bodyDocumentText,
            errorMessage))
    {
        return false;
    }

    state.currentSnapshotId = snapshot->snapshotId;
    VersionLimitManager::pruneOldestSnapshots(&state);
    return writeState(fileGateway, stateCodec, versionFilePath, state, errorMessage);
}

bool ThinkingSpaceLocalNoteVersionStore::rollbackToSnapshot(
    RollbackRequest request,
    ThinkingSpaceNoteVersionSnapshot* outSnapshot,
    QString* errorMessage) const
{
    const ThinkingSpaceNoteVersionFileGateway fileGateway;
    const ThinkingSpaceNoteVersionStateCodec stateCodec;
    const ThinkingSpaceNoteVersionSnapshotBuilder snapshotBuilder;
    const QString versionFilePath = QDir::cleanPath(request.versionFilePath.trimmed());

    ThinkingSpaceNoteVersionState state;
    if (!readState(fileGateway, stateCodec, versionFilePath, &state, errorMessage))
    {
        return false;
    }

    const ThinkingSpaceNoteVersionSnapshot* targetSnapshot =
        snapshotBuilder.findSnapshot(state, request.snapshotId);
    if (targetSnapshot == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to resolve rollback snapshot: %1").arg(request.snapshotId);
        }
        return false;
    }

    if (!fileGateway.writeUtf8File(
            QDir::cleanPath(request.noteHeaderPath.trimmed()),
            targetSnapshot->headerText,
            errorMessage)
        || !fileGateway.writeUtf8File(
            QDir::cleanPath(request.noteBodyPath.trimmed()),
            targetSnapshot->bodyDocumentText,
            errorMessage))
    {
        return false;
    }

    const QString label = request.label.trimmed().isEmpty()
                              ? QStringLiteral("rollback:%1").arg(targetSnapshot->snapshotId)
                              : request.label.trimmed();
    const QString parentSnapshotId = snapshotBuilder.parentSnapshotIdForCapture(state);
    ThinkingSpaceNoteVersionSnapshot rollbackSnapshot = snapshotBuilder.buildSnapshot(
        state,
        parentSnapshotId,
        targetSnapshot->snapshotId,
        label,
        request.commitModifiedCount,
        targetSnapshot->headerText,
        targetSnapshot->bodyDocumentText,
        targetSnapshot->bodyPlainText);

    state.snapshots.push_back(rollbackSnapshot);
    state.currentSnapshotId = rollbackSnapshot.snapshotId;
    state.headSnapshotId = rollbackSnapshot.snapshotId;
    VersionLimitManager::pruneOldestSnapshots(&state);

    if (!writeState(fileGateway, stateCodec, versionFilePath, state, errorMessage))
    {
        return false;
    }

    if (outSnapshot != nullptr)
    {
        *outSnapshot = std::move(rollbackSnapshot);
    }
    return true;
}
