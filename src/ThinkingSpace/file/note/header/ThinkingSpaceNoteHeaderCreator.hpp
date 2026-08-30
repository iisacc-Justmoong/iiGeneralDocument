#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/package/ThinkingSpaceNoteCreator.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderStore.hpp"

class ThinkingSpaceNoteHeaderCreator : public ThinkingSpaceNoteCreator
{
public:
    explicit ThinkingSpaceNoteHeaderCreator(QString workspaceRootPath, QString notesRootPath = QStringLiteral("notes"));
    ~ThinkingSpaceNoteHeaderCreator() override;

    QString creatorName() const override;
    QString targetPathForNote(const QString& noteId) const override;
    QStringList requiredRelativePaths() const override;

    QString headerFileName() const;
    QString createHeaderText(const ThinkingSpaceNoteHeaderStore& store) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
