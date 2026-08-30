#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/diff/ThinkingSpaceLocalNoteVersionStore.hpp"

class ThinkingSpaceNoteVersionSnapshotBuilder final
{
public:
    const ThinkingSpaceNoteVersionSnapshot* findSnapshot(
        const ThinkingSpaceNoteVersionState& state,
        const QString& snapshotId) const;

    QString parentSnapshotIdForCapture(const ThinkingSpaceNoteVersionState& state) const;

    ThinkingSpaceNoteVersionSnapshot buildSnapshot(
        const ThinkingSpaceNoteVersionState& state,
        const QString& parentSnapshotId,
        const QString& sourceSnapshotId,
        const QString& label,
        int commitModifiedCount,
        const QString& headerText,
        const QString& bodyDocumentText,
        const QString& bodyPlainText) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
