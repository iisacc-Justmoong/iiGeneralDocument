#include "ThinkingSpace/file/note/hub/ThinkingSpaceHubNoteCreationService.hpp"

#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderCreator.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderStore.hpp"
#include "ThinkingSpace/hierarchy/library/LibraryHierarchyControllerSupport.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

ThinkingSpaceHubNoteCreationService::ThinkingSpaceHubNoteCreationService() = default;

ThinkingSpaceHubNoteCreationService::~ThinkingSpaceHubNoteCreationService() = default;

bool ThinkingSpaceHubNoteCreationService::createNote(Request request, Result* outResult, QString* errorMessage) const
{
    const QString sourceWshubPath = ThinkingSpace::Hierarchy::LibrarySupport::normalizePath(request.tshubPath);
    QString libraryPath = ThinkingSpace::Hierarchy::LibrarySupport::normalizePath(request.libraryPath);
    QString resolveError;
    if (libraryPath.isEmpty() || !QFileInfo(libraryPath).isDir())
    {
        libraryPath = m_hubStructureValidator.resolvePrimaryLibraryPath(sourceWshubPath, &resolveError);
    }

    if (libraryPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = resolveError.isEmpty()
                                ? QStringLiteral("Failed to resolve library path for note creation.")
                                : resolveError;
        }
        return false;
    }

    const QString noteId = ThinkingSpace::NoteMutationSupport::createUniqueNoteId(libraryPath, request.notes);
    if (noteId.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to generate a unique note id.");
        }
        return false;
    }

    QStringList assignedFolders;
    assignedFolders.reserve(request.assignedFolders.size());
    for (const QString& rawFolder : request.assignedFolders)
    {
        const QString normalizedFolder = rawFolder.trimmed();
        if (!normalizedFolder.isEmpty())
        {
            assignedFolders.push_back(normalizedFolder);
        }
    }
    assignedFolders.removeDuplicates();
    QStringList assignedFolderUuids = request.assignedFolderUuids;
    while (assignedFolderUuids.size() < assignedFolders.size())
    {
        assignedFolderUuids.push_back(QString());
    }
    while (assignedFolderUuids.size() > assignedFolders.size())
    {
        assignedFolderUuids.removeLast();
    }

    const QString profileName = !request.authorProfileName.trimmed().isEmpty()
                                    ? request.authorProfileName.trimmed()
                                    : (request.hubStat.participants().isEmpty()
                                           ? QString()
                                           : request.hubStat.participants().constFirst().trimmed());

    ThinkingSpaceNoteHeaderCreator headerCreator(libraryPath, QString());

    const QString headerPath = headerCreator.targetPathForNote(noteId);
    const QString noteDirectoryPath = QFileInfo(headerPath).absolutePath();

    if (QFileInfo(noteDirectoryPath).exists())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Note directory already exists: %1").arg(noteDirectoryPath);
        }
        return false;
    }

    QString createError;
    if (!ThinkingSpace::NoteMutationSupport::ensureDirectoryPath(noteDirectoryPath, &createError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = createError;
        }
        return false;
    }

    auto rollbackNoteDirectory = [&noteDirectoryPath]()
    {
        if (QFileInfo(noteDirectoryPath).exists())
        {
            ThinkingSpace::NoteMutationSupport::removeDirectoryPath(noteDirectoryPath, nullptr);
        }
    };

    for (const QString& relativePath : headerCreator.requiredRelativePaths())
    {
        if (!ThinkingSpace::NoteMutationSupport::ensureDirectoryPath(
            QDir(noteDirectoryPath).filePath(relativePath),
            &createError))
        {
            rollbackNoteDirectory();
            if (errorMessage != nullptr)
            {
                *errorMessage = createError;
            }
            return false;
        }
    }

    const QString createdTimestamp = ThinkingSpace::NoteMutationSupport::currentNoteTimestamp();
    ThinkingSpaceNoteHeaderStore headerStore;
    headerStore.setNoteId(noteId);
    headerStore.setCreatedAt(createdTimestamp);
    headerStore.setAuthor(profileName);
    headerStore.setLastModifiedAt(createdTimestamp);
    headerStore.setModifiedBy(profileName);
    headerStore.setFolderBindings(assignedFolders, assignedFolderUuids);
    headerStore.setProject(QString());
    headerStore.setBookmarked(false);
    headerStore.setBookmarkColors({});
    headerStore.setTags({});
    headerStore.setProgress(-1);
    headerStore.setPreset(false);

    ThinkingSpaceLocalNoteFileStore::CreateRequest createRequest;
    createRequest.noteId = noteId;
    createRequest.noteDirectoryPath = noteDirectoryPath;
    createRequest.headerStore = headerStore;
    createRequest.bodyPlainText = QString();

    ThinkingSpaceLocalNoteDocument createdNoteDocument;
    if (!m_localNoteFileStore.createNote(std::move(createRequest), &createdNoteDocument, &createError))
    {
        rollbackNoteDirectory();
        if (errorMessage != nullptr)
        {
            *errorMessage = createError;
        }
        return false;
    }

    QVector<LibraryNoteRecord> nextAllNotes = request.notes;
    LibraryNoteRecord newNote = createdNoteDocument.toLibraryNoteRecord();
    if (newNote.noteId.trimmed().isEmpty())
    {
        newNote.noteId = noteId;
    }
    if (newNote.createdAt.trimmed().isEmpty())
    {
        newNote.createdAt = createdTimestamp;
    }
    if (newNote.lastModifiedAt.trimmed().isEmpty())
    {
        newNote.lastModifiedAt = createdTimestamp;
    }
    if (newNote.author.trimmed().isEmpty())
    {
        newNote.author = profileName;
    }
    if (newNote.modifiedBy.trimmed().isEmpty())
    {
        newNote.modifiedBy = profileName;
    }
    if (newNote.noteDirectoryPath.trimmed().isEmpty())
    {
        newNote.noteDirectoryPath = noteDirectoryPath;
    }
    if (newNote.noteHeaderPath.trimmed().isEmpty())
    {
        newNote.noteHeaderPath = headerPath;
    }
    nextAllNotes.push_back(newNote);

    const QString indexPath = QDir(libraryPath).filePath(QStringLiteral("index.tsnindex"));
    QString previousIndexText;
    const bool hadIndexFile = QFileInfo(indexPath).isFile();
    if (hadIndexFile && !ThinkingSpace::NoteMutationSupport::readUtf8File(indexPath, &previousIndexText, &createError))
    {
        rollbackNoteDirectory();
        if (errorMessage != nullptr)
        {
            *errorMessage = createError;
        }
        return false;
    }

    if (!m_libraryIndexIntegrityValidator.rewriteIndexesFromRecords(
        sourceWshubPath,
        QStringList{libraryPath},
        nextAllNotes,
        &createError))
    {
        rollbackNoteDirectory();
        if (errorMessage != nullptr)
        {
            *errorMessage = createError;
        }
        return false;
    }

    auto restoreIndexFile = [&]()
    {
        if (hadIndexFile)
        {
            ThinkingSpace::NoteMutationSupport::writeUtf8File(indexPath, previousIndexText, nullptr);
            return;
        }
        ThinkingSpace::NoteMutationSupport::removeFilePath(indexPath, nullptr);
    };

    QString statPath = ThinkingSpace::Hierarchy::LibrarySupport::normalizePath(request.statPath);
    if (statPath.isEmpty())
    {
        statPath = m_hubStructureValidator.resolveHubStatPath(sourceWshubPath);
    }

    const QString nowUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QString statCreatedAtUtc = request.hubStat.createdAtUtc().trimmed();
    QString previousStatText;
    const bool hadStatFile = !statPath.isEmpty() && QFileInfo(statPath).isFile();
    bool wroteStatFile = false;

    auto restoreStatFile = [&]()
    {
        if (statPath.isEmpty())
        {
            return;
        }
        if (hadStatFile)
        {
            ThinkingSpace::NoteMutationSupport::writeUtf8File(statPath, previousStatText, nullptr);
            return;
        }
        if (wroteStatFile)
        {
            ThinkingSpace::NoteMutationSupport::removeFilePath(statPath, nullptr);
        }
    };

    ThinkingSpaceHubStat updatedStat = request.hubStat;
    if (!statPath.isEmpty())
    {
        QJsonObject statRoot;
        if (hadStatFile)
        {
            QString rawStatText;
            if (!ThinkingSpace::NoteMutationSupport::readUtf8File(statPath, &rawStatText, &createError))
            {
                restoreIndexFile();
                rollbackNoteDirectory();
                if (errorMessage != nullptr)
                {
                    *errorMessage = createError;
                }
                return false;
            }

            previousStatText = rawStatText;
            QJsonParseError statParseError;
            const QJsonDocument statDocument = QJsonDocument::fromJson(rawStatText.toUtf8(), &statParseError);
            if (statParseError.error == QJsonParseError::NoError && statDocument.isObject())
            {
                statRoot = statDocument.object();
            }
        }

        if (statCreatedAtUtc.isEmpty())
        {
            statCreatedAtUtc = statRoot.value(QStringLiteral("createdAtUtc")).toString().trimmed();
        }
        if (statCreatedAtUtc.isEmpty())
        {
            statCreatedAtUtc = nowUtc;
        }

        if (!statRoot.contains(QStringLiteral("version")))
        {
            statRoot.insert(QStringLiteral("version"), 1);
        }
        if (!statRoot.contains(QStringLiteral("schema")))
        {
            statRoot.insert(QStringLiteral("schema"), QStringLiteral("thinkingspace.hub.stat"));
        }
        if (!statRoot.contains(QStringLiteral("hub")) && !request.hubName.trimmed().isEmpty())
        {
            statRoot.insert(QStringLiteral("hub"), request.hubName.trimmed());
        }
        if (!statRoot.contains(QStringLiteral("participants")))
        {
            QJsonArray participants;
            for (const QString& participant : request.hubStat.participants())
            {
                participants.push_back(participant);
            }
            statRoot.insert(QStringLiteral("participants"), participants);
        }
        if (!statRoot.contains(QStringLiteral("profileLastModifiedAtUtc")))
        {
            statRoot.insert(
                QStringLiteral("profileLastModifiedAtUtc"),
                QJsonObject::fromVariantMap(request.hubStat.profileLastModifiedAtUtc()));
        }

        statRoot.insert(QStringLiteral("noteCount"), nextAllNotes.size());
        statRoot.insert(QStringLiteral("resourceCount"), request.hubStat.resourceCount());
        statRoot.insert(QStringLiteral("characterCount"), request.hubStat.characterCount());
        statRoot.insert(QStringLiteral("createdAtUtc"), statCreatedAtUtc);
        statRoot.insert(QStringLiteral("lastModifiedAtUtc"), nowUtc);

        if (!ThinkingSpace::NoteMutationSupport::writeUtf8File(
            statPath,
            QString::fromUtf8(QJsonDocument(statRoot).toJson(QJsonDocument::Indented)),
            &createError))
        {
            restoreIndexFile();
            restoreStatFile();
            rollbackNoteDirectory();
            if (errorMessage != nullptr)
            {
                *errorMessage = createError;
            }
            return false;
        }
        wroteStatFile = true;
    }

    updatedStat.setNoteCount(nextAllNotes.size());
    if (updatedStat.createdAtUtc().trimmed().isEmpty())
    {
        updatedStat.setCreatedAtUtc(statCreatedAtUtc.isEmpty() ? nowUtc : statCreatedAtUtc);
    }
    updatedStat.setLastModifiedAtUtc(nowUtc);

    if (outResult != nullptr)
    {
        outResult->noteId = noteId;
        outResult->tshubPath = sourceWshubPath;
        outResult->libraryPath = libraryPath;
        outResult->statPath = statPath;
        outResult->hubStat = updatedStat;
        outResult->notes = std::move(nextAllNotes);
    }

    ThinkingSpace::Debug::trace(
        QStringLiteral("note.create.service"),
        QStringLiteral("createNote.success"),
        QStringLiteral("id=%1 folderCount=%2 noteCount=%3 path=%4")
            .arg(noteId)
            .arg(assignedFolders.size())
            .arg(request.notes.size() + 1)
            .arg(noteDirectoryPath));
    return true;
}
