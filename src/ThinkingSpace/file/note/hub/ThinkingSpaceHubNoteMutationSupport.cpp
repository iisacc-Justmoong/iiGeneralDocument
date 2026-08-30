#include "ThinkingSpace/file/note/hub/ThinkingSpaceHubNoteMutationSupport.hpp"

#include "ThinkingSpace/hierarchy/library/LibraryHierarchyControllerSupport.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSet>

namespace
{
    constexpr auto kNoteTimestampFormat = "yyyy-MM-dd-hh-mm-ss";

    QString normalizeFileSystemPath(QString path)
    {
        path = path.trimmed();
        if (path.isEmpty())
        {
            return {};
        }
        return QDir::cleanPath(path);
    }

    QString randomAlphaNumericSegment(int length)
    {
        static const QString upperAlphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        static const QString lowerAlphabet = QStringLiteral("abcdefghijklmnopqrstuvwxyz");
        static const QString digits = QStringLiteral("0123456789");
        static const QString alphaNumericAlphabet = upperAlphabet + lowerAlphabet + digits;

        if (length <= 0)
        {
            return {};
        }

        QString segment;
        segment.reserve(length);
        segment.push_back(upperAlphabet.at(QRandomGenerator::global()->bounded(upperAlphabet.size())));
        if (length > 1)
        {
            segment.push_back(lowerAlphabet.at(QRandomGenerator::global()->bounded(lowerAlphabet.size())));
        }
        if (length > 2)
        {
            segment.push_back(digits.at(QRandomGenerator::global()->bounded(digits.size())));
        }
        for (int index = segment.size(); index < length; ++index)
        {
            segment.push_back(
                alphaNumericAlphabet.at(QRandomGenerator::global()->bounded(alphaNumericAlphabet.size())));
        }

        for (int index = segment.size() - 1; index > 0; --index)
        {
            const int swapIndex = QRandomGenerator::global()->bounded(index + 1);
            if (swapIndex != index)
            {
                const QChar currentValue = segment.at(index);
                segment[index] = segment.at(swapIndex);
                segment[swapIndex] = currentValue;
            }
        }

        return segment;
    }
} // namespace

QString ThinkingSpace::NoteMutationSupport::currentNoteTimestamp()
{
    return QDateTime::currentDateTime().toString(QString::fromLatin1(kNoteTimestampFormat));
}

int ThinkingSpace::NoteMutationSupport::indexOfNoteRecordById(
    const QVector<LibraryNoteRecord>& notes,
    const QString& noteId)
{
    const QString normalizedNoteId = noteId.trimmed();
    if (normalizedNoteId.isEmpty())
    {
        return -1;
    }

    for (int index = 0; index < notes.size(); ++index)
    {
        if (notes.at(index).noteId.trimmed() == normalizedNoteId)
        {
            return index;
        }
    }

    return -1;
}

QString ThinkingSpace::NoteMutationSupport::createUniqueNoteId(
    const QString& libraryPath,
    const QVector<LibraryNoteRecord>& existingNotes)
{
    QSet<QString> existingKeys;
    existingKeys.reserve(existingNotes.size());
    for (const LibraryNoteRecord& note : existingNotes)
    {
        const QString noteIdKey = note.noteId.trimmed().toCaseFolded();
        if (!noteIdKey.isEmpty())
        {
            existingKeys.insert(noteIdKey);
        }
    }

    const QDir libraryDir(libraryPath);
    for (int attempt = 0; attempt < 4096; ++attempt)
    {
        const QString candidate = randomAlphaNumericSegment(16) + QLatin1Char('-')
            + randomAlphaNumericSegment(16);
        const QString candidateKey = candidate.toCaseFolded();
        if (existingKeys.contains(candidateKey))
        {
            continue;
        }

        if (libraryDir.exists(candidate + QStringLiteral(".tsnote")))
        {
            continue;
        }

        return candidate;
    }

    return {};
}

bool ThinkingSpace::NoteMutationSupport::ensureDirectoryPath(const QString& directoryPath, QString* errorMessage)
{
    const QString normalizedPath = normalizeFileSystemPath(directoryPath);
    if (normalizedPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Directory path must not be empty.");
        }
        return false;
    }

    if (QDir(normalizedPath).exists())
    {
        return true;
    }

    QDir directory;
    if (directory.mkpath(normalizedPath))
    {
        return true;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = QStringLiteral("Failed to create directory: %1").arg(normalizedPath);
    }
    return false;
}

bool ThinkingSpace::NoteMutationSupport::readUtf8File(
    const QString& filePath,
    QString* outText,
    QString* errorMessage)
{
    if (outText == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("outText must not be null.");
        }
        return false;
    }

    const QString normalizedPath = normalizeFileSystemPath(filePath);
    if (normalizedPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("File path must not be empty.");
        }
        return false;
    }

    QFile file(normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to open file for reading: %1 (%2)")
                .arg(normalizedPath, file.errorString());
        }
        return false;
    }

    *outText = QString::fromUtf8(file.readAll());
    return true;
}

bool ThinkingSpace::NoteMutationSupport::writeUtf8File(
    const QString& filePath,
    const QString& text,
    QString* errorMessage)
{
    const QString normalizedPath = normalizeFileSystemPath(filePath);
    if (normalizedPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("File path must not be empty.");
        }
        return false;
    }

    const QString parentPath = QFileInfo(normalizedPath).absolutePath();
    if (!parentPath.isEmpty())
    {
        QString ensureError;
        if (!ensureDirectoryPath(parentPath, &ensureError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = ensureError;
            }
            return false;
        }
    }

    QSaveFile file(normalizedPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to open file for writing: %1 (%2)")
                .arg(normalizedPath, file.errorString());
        }
        return false;
    }

    const QByteArray utf8Bytes = text.toUtf8();
    const qint64 bytesWritten = file.write(utf8Bytes);
    if (bytesWritten != utf8Bytes.size())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to write UTF-8 bytes: %1 (%2)")
                .arg(normalizedPath, file.errorString());
        }
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to commit UTF-8 file: %1 (%2)")
                .arg(normalizedPath, file.errorString());
        }
        return false;
    }

    return true;
}

bool ThinkingSpace::NoteMutationSupport::removeFilePath(const QString& filePath, QString* errorMessage)
{
    const QString normalizedPath = normalizeFileSystemPath(filePath);
    if (normalizedPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("File path must not be empty.");
        }
        return false;
    }

    QFile file(normalizedPath);
    if (!file.exists())
    {
        return true;
    }
    if (file.remove())
    {
        return true;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = QStringLiteral("Failed to remove file: %1 (%2)")
            .arg(normalizedPath, file.errorString());
    }
    return false;
}

bool ThinkingSpace::NoteMutationSupport::removeDirectoryPath(const QString& directoryPath, QString* errorMessage)
{
    const QString normalizedPath = normalizeFileSystemPath(directoryPath);
    if (normalizedPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Directory path must not be empty.");
        }
        return false;
    }

    QDir directory(normalizedPath);
    if (!directory.exists())
    {
        return true;
    }
    if (directory.removeRecursively())
    {
        return true;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = QStringLiteral("Failed to remove directory recursively: %1").arg(normalizedPath);
    }
    return false;
}

bool ThinkingSpace::NoteMutationSupport::pathExists(const QString& path)
{
    const QString normalizedPath = normalizeFileSystemPath(path);
    return !normalizedPath.isEmpty() && QFileInfo::exists(normalizedPath);
}

QString ThinkingSpace::NoteMutationSupport::resolveNoteHeaderPath(const LibraryNoteRecord& note)
{
    const QString directPath = ThinkingSpace::Hierarchy::LibrarySupport::normalizePath(note.noteHeaderPath);
    if (!directPath.isEmpty() && QFileInfo(directPath).isFile())
    {
        return directPath;
    }

    const QString noteDirectoryPath = ThinkingSpace::Hierarchy::LibrarySupport::normalizePath(note.noteDirectoryPath);
    if (noteDirectoryPath.isEmpty())
    {
        return {};
    }

    const QDir noteDir(noteDirectoryPath);
    if (!noteDir.exists())
    {
        return {};
    }

    const QString noteStem = QFileInfo(noteDirectoryPath).completeBaseName().trimmed();
    if (!noteStem.isEmpty())
    {
        const QString stemHeaderPath = noteDir.filePath(noteStem + QStringLiteral(".tsnhead"));
        if (QFileInfo(stemHeaderPath).isFile())
        {
            return QDir::cleanPath(stemHeaderPath);
        }
    }

    const QString canonicalHeaderPath = noteDir.filePath(QStringLiteral("note.tsnhead"));
    if (QFileInfo(canonicalHeaderPath).isFile())
    {
        return QDir::cleanPath(canonicalHeaderPath);
    }

    const QFileInfoList headerCandidates = noteDir.entryInfoList(
        QStringList{QStringLiteral("*.tsnhead")},
        QDir::Files,
        QDir::Name);
    QString draftHeaderPath;
    for (const QFileInfo& fileInfo : headerCandidates)
    {
        const QString loweredName = fileInfo.fileName().toCaseFolded();
        if (loweredName.contains(QStringLiteral(".draft.")))
        {
            if (draftHeaderPath.isEmpty())
            {
                draftHeaderPath = fileInfo.absoluteFilePath();
            }
            continue;
        }
        return QDir::cleanPath(fileInfo.absoluteFilePath());
    }

    if (!draftHeaderPath.isEmpty())
    {
        return QDir::cleanPath(draftHeaderPath);
    }

    return {};
}

void ThinkingSpace::NoteMutationSupport::syncNoteRecordFromDocument(
    LibraryNoteRecord* note,
    const ThinkingSpaceLocalNoteDocument& document)
{
    if (note == nullptr)
    {
        return;
    }

    const LibraryNoteRecord updatedRecord = document.toLibraryNoteRecord();
    note->folders = updatedRecord.folders;
    note->folderUuids = updatedRecord.folderUuids;
    note->lastModifiedAt = updatedRecord.lastModifiedAt;
    note->noteHeaderPath = updatedRecord.noteHeaderPath;
    if (note->noteDirectoryPath.isEmpty())
    {
        note->noteDirectoryPath = updatedRecord.noteDirectoryPath;
    }
}
