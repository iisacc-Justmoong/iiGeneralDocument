#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/package/ThinkingSpaceNoteCreator.hpp"

class ThinkingSpaceNoteBodyCreator : public ThinkingSpaceNoteCreator
{
public:
    explicit ThinkingSpaceNoteBodyCreator(QString workspaceRootPath, QString notesRootPath = QStringLiteral("notes"));
    ~ThinkingSpaceNoteBodyCreator() override;

    QString creatorName() const override;
    QString targetPathForNote(const QString& noteId) const override;
    QStringList requiredRelativePaths() const override;

    QString bodyFileName() const;
    QString draftBodyFileName() const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
