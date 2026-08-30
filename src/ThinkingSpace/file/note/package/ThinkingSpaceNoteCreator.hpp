#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

class ThinkingSpaceNoteCreator
{
public:
    explicit ThinkingSpaceNoteCreator(QString workspaceRootPath, QString notesRootPath = QStringLiteral("notes"));
    virtual ~ThinkingSpaceNoteCreator();

    ThinkingSpaceNoteCreator(const ThinkingSpaceNoteCreator&) = delete;
    ThinkingSpaceNoteCreator& operator=(const ThinkingSpaceNoteCreator&) = delete;
    ThinkingSpaceNoteCreator(ThinkingSpaceNoteCreator&&) = default;
    ThinkingSpaceNoteCreator& operator=(ThinkingSpaceNoteCreator&&) = default;

    void setWorkspaceRootPath(QString workspaceRootPath);
    const QString& workspaceRootPath() const noexcept;

    void setNotesRootPath(QString notesRootPath);
    const QString& notesRootPath() const noexcept;

    virtual QString creatorName() const = 0;
    virtual QString targetPathForNote(const QString& noteId) const = 0;
    virtual QStringList requiredRelativePaths() const = 0;

protected:
    QString noteDirectoryPath(const QString& noteId) const;
    QString joinPath(const QString& left, const QString& right) const;

private:
    QString m_workspaceRootPath;
    QString m_notesRootPath;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
