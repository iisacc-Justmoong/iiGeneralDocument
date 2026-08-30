#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderStore.hpp"

#include <QString>

class ThinkingSpaceNoteHeaderParser
{
public:
    ThinkingSpaceNoteHeaderParser();
    ~ThinkingSpaceNoteHeaderParser();

    bool parse(
        const QString& tsnHeadText,
        ThinkingSpaceNoteHeaderStore* outStore,
        QString* errorMessage = nullptr) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
