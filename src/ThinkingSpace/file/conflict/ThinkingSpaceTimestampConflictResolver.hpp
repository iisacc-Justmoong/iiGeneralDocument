#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>

class ThinkingSpaceTimestampConflictResolver final
{
public:
    struct MergeRequest final
    {
        QString baseLastModifiedAt;
        QString filesystemLastModifiedAt;
        QString incomingLastModifiedAt;
        QString filesystemBodySourceText;
        QString incomingBodySourceText;
    };

    struct MergeResult final
    {
        QString mergedBodySourceText;
        QString winner;
        QString winningLastModifiedAt;
        bool conflictDetected = false;
    };

    MergeResult mergeBodyByTimestamp(const MergeRequest& request) const;
    bool isTimestampNewer(
        const QString& candidateLastModifiedAt,
        const QString& baselineLastModifiedAt) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
