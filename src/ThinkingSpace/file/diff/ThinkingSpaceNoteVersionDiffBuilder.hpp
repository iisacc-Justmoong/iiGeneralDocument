#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/diff/ThinkingSpaceLocalNoteVersionStore.hpp"

class ThinkingSpaceNoteVersionDiffBuilder final
{
public:
    ThinkingSpaceNoteVersionDiffSegment diffSegment(
        const QString& before,
        const QString& after,
        const QString& label) const;
    QString applyDiffSegmentOntoCurrent(
        const QString& base,
        const QString& current,
        const ThinkingSpaceNoteVersionDiffSegment& segment,
        bool* applied = nullptr,
        QString* errorMessage = nullptr) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
