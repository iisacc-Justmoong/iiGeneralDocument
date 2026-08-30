#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/diff/ThinkingSpaceLocalNoteVersionStore.hpp"

class VersionLimitManager final
{
public:
    static int maximumSnapshotCount() noexcept;
    static int pruneOldestSnapshots(ThinkingSpaceNoteVersionState* state);
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
