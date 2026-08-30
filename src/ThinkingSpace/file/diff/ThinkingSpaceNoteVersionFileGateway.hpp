#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteDocument.hpp"

#include <QString>

class ThinkingSpaceNoteVersionFileGateway final
{
public:
    QString noteIdFromDocument(const ThinkingSpaceLocalNoteDocument& document) const;
    QString versionPathFromDocument(const ThinkingSpaceLocalNoteDocument& document) const;
    QString headerPathFromDocument(const ThinkingSpaceLocalNoteDocument& document) const;
    QString bodyPathFromDocument(const ThinkingSpaceLocalNoteDocument& document) const;

    bool readUtf8File(
        const QString& path,
        QString* outText,
        QString* errorMessage = nullptr) const;
    bool writeUtf8File(
        const QString& path,
        const QString& text,
        QString* errorMessage = nullptr) const;
    bool ensureVersionDocument(
        const QString& versionFilePath,
        const QString& emptyVersionText,
        QString* errorMessage = nullptr) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
