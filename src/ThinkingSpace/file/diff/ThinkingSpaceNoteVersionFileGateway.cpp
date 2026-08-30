#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionFileGateway.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace
{
    QString noteStemFromDocument(const ThinkingSpaceLocalNoteDocument& document, const QString& fallbackNoteId)
    {
        QString stem = QFileInfo(document.noteDirectoryPath).completeBaseName().trimmed();
        if (stem.isEmpty())
        {
            stem = QFileInfo(fallbackNoteId).completeBaseName().trimmed();
        }
        return stem.isEmpty() ? QStringLiteral("note") : stem;
    }
} // namespace

QString ThinkingSpaceNoteVersionFileGateway::noteIdFromDocument(const ThinkingSpaceLocalNoteDocument& document) const
{
    QString noteId = document.headerStore.noteId().trimmed();
    if (!noteId.isEmpty())
    {
        return noteId;
    }

    noteId = QFileInfo(document.noteDirectoryPath).completeBaseName().trimmed();
    if (!noteId.isEmpty())
    {
        return noteId;
    }

    return QStringLiteral("note");
}

QString ThinkingSpaceNoteVersionFileGateway::versionPathFromDocument(const ThinkingSpaceLocalNoteDocument& document) const
{
    const QString directPath = QDir::cleanPath(document.noteVersionPath.trimmed());
    if (!directPath.isEmpty())
    {
        return directPath;
    }

    const QString noteDirectoryPath = QDir::cleanPath(document.noteDirectoryPath.trimmed());
    if (noteDirectoryPath.isEmpty())
    {
        return {};
    }

    return QDir(noteDirectoryPath)
        .filePath(noteStemFromDocument(document, noteIdFromDocument(document)) + QStringLiteral(".tsnversion"));
}

QString ThinkingSpaceNoteVersionFileGateway::headerPathFromDocument(const ThinkingSpaceLocalNoteDocument& document) const
{
    const QString directPath = QDir::cleanPath(document.noteHeaderPath.trimmed());
    if (!directPath.isEmpty())
    {
        return directPath;
    }

    const QString noteDirectoryPath = QDir::cleanPath(document.noteDirectoryPath.trimmed());
    if (noteDirectoryPath.isEmpty())
    {
        return {};
    }

    return QDir(noteDirectoryPath)
        .filePath(noteStemFromDocument(document, noteIdFromDocument(document)) + QStringLiteral(".tsnhead"));
}

QString ThinkingSpaceNoteVersionFileGateway::bodyPathFromDocument(const ThinkingSpaceLocalNoteDocument& document) const
{
    const QString directPath = QDir::cleanPath(document.noteBodyPath.trimmed());
    if (!directPath.isEmpty())
    {
        return directPath;
    }

    const QString noteDirectoryPath = QDir::cleanPath(document.noteDirectoryPath.trimmed());
    if (noteDirectoryPath.isEmpty())
    {
        return {};
    }

    return QDir(noteDirectoryPath)
        .filePath(noteStemFromDocument(document, noteIdFromDocument(document)) + QStringLiteral(".tsnbody"));
}

bool ThinkingSpaceNoteVersionFileGateway::readUtf8File(
    const QString& path,
    QString* outText,
    QString* errorMessage) const
{
    if (outText == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("outText must not be null.");
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to open file for reading: %1").arg(path);
        }
        return false;
    }

    *outText = QString::fromUtf8(file.readAll());
    return true;
}

bool ThinkingSpaceNoteVersionFileGateway::writeUtf8File(
    const QString& path,
    const QString& text,
    QString* errorMessage) const
{
    const QFileInfo fileInfo(path);
    if (fileInfo.absolutePath().trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to resolve parent directory for: %1").arg(path);
        }
        return false;
    }

    QDir parentDirectory(fileInfo.absolutePath());
    if (!parentDirectory.exists() && !QDir().mkpath(fileInfo.absolutePath()))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to create parent directory for: %1").arg(path);
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to open file for writing: %1").arg(path);
        }
        return false;
    }
    file.write(text.toUtf8());
    if (!file.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to commit file write: %1").arg(path);
        }
        return false;
    }
    return true;
}

bool ThinkingSpaceNoteVersionFileGateway::ensureVersionDocument(
    const QString& versionFilePath,
    const QString& emptyVersionText,
    QString* errorMessage) const
{
    if (versionFilePath.trimmed().isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("versionFilePath must not be empty.");
        }
        return false;
    }

    if (QFileInfo(versionFilePath).isFile())
    {
        return true;
    }

    return writeUtf8File(versionFilePath, emptyVersionText, errorMessage);
}
