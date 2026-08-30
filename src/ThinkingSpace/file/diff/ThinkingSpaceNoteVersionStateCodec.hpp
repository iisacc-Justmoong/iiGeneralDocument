#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/diff/ThinkingSpaceLocalNoteVersionStore.hpp"

class ThinkingSpaceNoteVersionStateCodec final
{
public:
    QString emptyStateText(const QString& noteId) const;
    QString serializeState(const ThinkingSpaceNoteVersionState& state) const;
    bool parseState(
        const QString& versionText,
        const QString& versionFilePath,
        ThinkingSpaceNoteVersionState* outState,
        QString* errorMessage = nullptr) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
