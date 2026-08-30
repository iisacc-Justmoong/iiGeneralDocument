#include "ThinkingSpace/file/note/package/ThinkingSpaceNoteBodyCreator.hpp"

#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"

#include <QFileInfo>

#include <utility>

ThinkingSpaceNoteBodyCreator::ThinkingSpaceNoteBodyCreator(QString workspaceRootPath, QString notesRootPath)
    : ThinkingSpaceNoteCreator(std::move(workspaceRootPath), std::move(notesRootPath))
{
}

ThinkingSpaceNoteBodyCreator::~ThinkingSpaceNoteBodyCreator() = default;

QString ThinkingSpaceNoteBodyCreator::creatorName() const
{
    return QStringLiteral("ThinkingSpaceNoteBodyCreator");
}

QString ThinkingSpaceNoteBodyCreator::targetPathForNote(const QString& noteId) const
{
    const QString noteDirPath = noteDirectoryPath(noteId);
    const QString noteStem = QFileInfo(noteDirPath).completeBaseName();
    const QString fileName = noteStem + QStringLiteral(".tsnbody");
    const QString targetPath = joinPath(noteDirPath, fileName);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.creator.body"),
                              QStringLiteral("targetPathForNote"),
                              QStringLiteral("noteId=%1 path=%2").arg(noteId, targetPath));
    return targetPath;
}

QStringList ThinkingSpaceNoteBodyCreator::requiredRelativePaths() const
{
    return {};
}

QString ThinkingSpaceNoteBodyCreator::bodyFileName() const
{
    return QStringLiteral("note.tsnbody");
}

QString ThinkingSpaceNoteBodyCreator::draftBodyFileName() const
{
    return QStringLiteral("note.draft.tsnbody");
}
