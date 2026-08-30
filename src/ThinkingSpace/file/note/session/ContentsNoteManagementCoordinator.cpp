#include "ThinkingSpace/file/note/session/ContentsNoteManagementCoordinator.hpp"

#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"
#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionDiffBuilder.hpp"
#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteFileStore.hpp"
#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"
#include "ThinkingSpace/file/statistic/ThinkingSpaceNoteFileStatSupport.hpp"
#include "ThinkingSpace/platform/Android/ThinkingSpaceAndroidStorageBackend.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QThreadPool>

namespace
{
    constexpr auto kApplyPersistedBodyStateForNoteSignature =
        "applyPersistedBodyStateForNote(QString,QString,QString,QString)";
    constexpr auto kNoteDirectoryPathForNoteIdSignature = "noteDirectoryPathForNoteId(QString)";
    constexpr auto kRequestControllerHookSignature = "requestControllerHook()";
    constexpr auto kReloadNoteMetadataForNoteIdSignature = "reloadNoteMetadataForNoteId(QString)";
    constexpr auto kSaveBodyTextForNoteSignature = "saveBodyTextForNote(QString,QString)";
    constexpr auto kSaveCurrentBodyTextSignature = "saveCurrentBodyText(QString)";

    bool isWholeDocumentReplacementDiff(
        const QString& baseBodySourceText,
        const ThinkingSpaceNoteVersionDiffSegment& bodyDiff)
    {
        return bodyDiff.prefixLength == 0
            && bodyDiff.suffixLength == 0
            && bodyDiff.removedText == baseBodySourceText
            && !baseBodySourceText.isEmpty();
    }
} // namespace

namespace
{
    bool syncMountedNotePathToSource(const QString& localPath, QString* errorMessage)
    {
        const QString normalizedPath = QDir::cleanPath(localPath.trimmed());
        if (normalizedPath.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                errorMessage->clear();
            }
            return true;
        }

        return ThinkingSpace::Android::Storage::syncLocalPathToSource(normalizedPath, errorMessage);
    }
} // namespace

ContentsNoteManagementCoordinator::ContentsNoteManagementCoordinator(QObject* parent)
    : QObject(parent)
{
}

ContentsNoteManagementCoordinator::~ContentsNoteManagementCoordinator() = default;

QObject* ContentsNoteManagementCoordinator::contentController() const noexcept
{
    return m_contentController;
}

void ContentsNoteManagementCoordinator::setContentController(QObject* model)
{
    if (m_contentController == model)
    {
        return;
    }

    if (m_contentControllerDestroyedConnection)
    {
        disconnect(m_contentControllerDestroyedConnection);
        m_contentControllerDestroyedConnection = QMetaObject::Connection();
    }

    resetBoundNotePersistenceSession();
    m_contentController = model;

    if (m_contentController != nullptr)
    {
        m_contentControllerDestroyedConnection = connect(
            m_contentController,
            &QObject::destroyed,
            this,
            &ContentsNoteManagementCoordinator::handleContentControllerDestroyed);
    }

    refreshContentPersistenceState();
}

bool ContentsNoteManagementCoordinator::contentPersistenceContractAvailable() const noexcept
{
    return m_contentPersistenceContractAvailable;
}

bool ContentsNoteManagementCoordinator::directPersistenceAvailable() const noexcept
{
    return m_directPersistenceContractAvailable;
}

bool ContentsNoteManagementCoordinator::persistEditorTextForNote(const QString& noteId, const QString& text)
{
    const QString normalizedNoteId = noteId.trimmed();
    if (normalizedNoteId.isEmpty() || !m_contentPersistenceContractAvailable)
    {
        return false;
    }

    return enqueuePersistenceRequest(normalizedNoteId, text);
}

bool ContentsNoteManagementCoordinator::persistEditorTextForNoteAtPath(
    const QString& noteId,
    const QString& noteDirectoryPath,
    const QString& text,
    const QString& baseLastModifiedAt,
    const QString& baseBodySourceText,
    const bool hasBaseBodySourceText)
{
    const QString normalizedNoteId = noteId.trimmed();
    const QString normalizedNoteDirectoryPath = noteDirectoryPath.trimmed();
    if (normalizedNoteId.isEmpty() || normalizedNoteDirectoryPath.isEmpty())
    {
        return false;
    }

    Request request;
    request.kind = RequestKind::DirectPersistBody;
    request.sequence = m_nextRequestSequence++;
    request.noteId = normalizedNoteId;
    request.noteDirectoryPath = normalizedNoteDirectoryPath;
    request.text = text;
    request.baseLastModifiedAt = baseLastModifiedAt.trimmed();
    request.baseBodySourceText = baseBodySourceText;
    request.hasBaseBodySourceText = hasBaseBodySourceText;
    return enqueueRequest(std::move(request));
}

bool ContentsNoteManagementCoordinator::captureDirectPersistenceContextForNote(
    const QString& noteId,
    QString* noteDirectoryPath) const
{
    if (noteDirectoryPath != nullptr)
    {
        noteDirectoryPath->clear();
    }

    const QString normalizedNoteId = noteId.trimmed();
    if (normalizedNoteId.isEmpty() || !m_directPersistenceContractAvailable)
    {
        return false;
    }

    const QString resolvedNoteDirectoryPath = resolveNoteDirectoryPathForNote(normalizedNoteId);
    if (resolvedNoteDirectoryPath.isEmpty())
    {
        return false;
    }

    if (noteDirectoryPath != nullptr)
    {
        *noteDirectoryPath = resolvedNoteDirectoryPath;
    }
    return true;
}

QString ContentsNoteManagementCoordinator::noteDirectoryPathForNote(const QString& noteId) const
{
    return resolveNoteDirectoryPathForNote(noteId.trimmed());
}

quint64 ContentsNoteManagementCoordinator::loadNoteBodyTextForNote(
    const QString& noteId,
    const QString& noteDirectoryPath)
{
    const QString normalizedNoteId = noteId.trimmed();
    if (normalizedNoteId.isEmpty())
    {
        return 0;
    }

    const QString resolvedNoteDirectoryPath =
        resolvePreferredNoteDirectoryPath(normalizedNoteId, noteDirectoryPath);
    if (resolvedNoteDirectoryPath.isEmpty())
    {
        return 0;
    }

    Request request;
    request.kind = RequestKind::LoadNoteBodyText;
    request.sequence = m_nextRequestSequence++;
    request.noteId = normalizedNoteId;
    request.noteDirectoryPath = resolvedNoteDirectoryPath;
    const quint64 requestSequence = request.sequence;
    if (!enqueueRequest(std::move(request)))
    {
        return 0;
    }
    return requestSequence;
}

bool ContentsNoteManagementCoordinator::reconcileViewSessionAndRefreshSnapshotForNote(
    const QString& noteId,
    const QString& viewSessionText,
    const bool preferViewSessionOnMismatch,
    const QString& noteDirectoryPath)
{
    const QString normalizedNoteId = noteId.trimmed();
    if (normalizedNoteId.isEmpty())
    {
        return false;
    }

    const QString resolvedNoteDirectoryPath =
        resolvePreferredNoteDirectoryPath(normalizedNoteId, noteDirectoryPath);
    if (resolvedNoteDirectoryPath.isEmpty())
    {
        return false;
    }

    Request request;
    request.kind = RequestKind::ReconcileViewSessionSnapshot;
    request.sequence = m_nextRequestSequence++;
    request.noteId = normalizedNoteId;
    request.noteDirectoryPath = resolvedNoteDirectoryPath;
    request.text = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(viewSessionText);
    request.preferViewSessionOnMismatch = preferViewSessionOnMismatch;
    return enqueueRequest(std::move(request));
}

bool ContentsNoteManagementCoordinator::refreshNoteSnapshotForNote(const QString& noteId)
{
    const QString normalizedNoteId = noteId.trimmed();
    if (normalizedNoteId.isEmpty() || !hasInvokableMethod(m_contentController, kReloadNoteMetadataForNoteIdSignature))
    {
        return false;
    }

    bool reloaded = false;
    if (!QMetaObject::invokeMethod(
            m_contentController,
            "reloadNoteMetadataForNoteId",
            Qt::DirectConnection,
            Q_RETURN_ARG(bool, reloaded),
            Q_ARG(QString, normalizedNoteId))
        || !reloaded)
    {
        return false;
    }

    if (m_boundNoteId == normalizedNoteId)
    {
        const QString reboundNoteId = m_boundNoteId;
        resetBoundNotePersistenceSession();
        ensureBoundNotePersistenceSession(reboundNoteId, m_boundNoteDirectoryPath, nullptr);
    }
    return true;
}

void ContentsNoteManagementCoordinator::bindSelectedNote(
    const QString& noteId,
    const QString& noteDirectoryPath)
{
    const QString normalizedNoteId = noteId.trimmed();
    if (normalizedNoteId.isEmpty())
    {
        resetBoundNotePersistenceSession();
        return;
    }

    QString sessionError;
    if (!ensureBoundNotePersistenceSession(normalizedNoteId, noteDirectoryPath, &sessionError))
    {
        ThinkingSpace::Debug::traceSelf(
            this,
            QStringLiteral("content.note.management"),
            QStringLiteral("bindSelectedNote.failed"),
            QStringLiteral("noteId=%1 error=%2").arg(normalizedNoteId, sessionError));
        return;
    }

    enqueueOpenCountIncrement(normalizedNoteId, m_boundNoteDirectoryPath);
}

void ContentsNoteManagementCoordinator::clearSelectedNote()
{
    resetBoundNotePersistenceSession();
}

void ContentsNoteManagementCoordinator::handleContentControllerDestroyed()
{
    if (m_contentControllerDestroyedConnection)
    {
        disconnect(m_contentControllerDestroyedConnection);
        m_contentControllerDestroyedConnection = QMetaObject::Connection();
    }

    resetBoundNotePersistenceSession();
    m_contentController = nullptr;
    refreshContentPersistenceState();
}

bool ContentsNoteManagementCoordinator::hasInvokableMethod(
    const QObject* object,
    const char* methodSignature)
{
    if (object == nullptr || methodSignature == nullptr)
    {
        return false;
    }

    return object->metaObject()->indexOfMethod(QMetaObject::normalizedSignature(methodSignature)) >= 0;
}

QString ContentsNoteManagementCoordinator::resolveNoteDirectoryPathForNote(const QString& noteId) const
{
    if (m_contentController == nullptr
        || !hasInvokableMethod(m_contentController, kNoteDirectoryPathForNoteIdSignature))
    {
        return {};
    }

    QString noteDirectoryPath;
    if (!QMetaObject::invokeMethod(
            m_contentController,
            "noteDirectoryPathForNoteId",
            Qt::DirectConnection,
            Q_RETURN_ARG(QString, noteDirectoryPath),
            Q_ARG(QString, noteId.trimmed())))
    {
        return {};
    }

    return noteDirectoryPath.trimmed();
}

QString ContentsNoteManagementCoordinator::resolvePreferredNoteDirectoryPath(
    const QString& noteId,
    const QString& noteDirectoryPath) const
{
    const QString normalizedPreferredDirectoryPath = QDir::cleanPath(noteDirectoryPath.trimmed());
    if (!normalizedPreferredDirectoryPath.isEmpty()
        && normalizedPreferredDirectoryPath != QStringLiteral("."))
    {
        return normalizedPreferredDirectoryPath;
    }

    return resolveNoteDirectoryPathForNote(noteId);
}

bool ContentsNoteManagementCoordinator::ensureBoundNotePersistenceSession(
    const QString& noteId,
    const QString& noteDirectoryPath,
    QString* errorMessage)
{
    const QString normalizedNoteId = noteId.trimmed();
    if (normalizedNoteId.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("noteId must not be empty.");
        }
        return false;
    }

    const QString resolvedDirectoryPath =
        resolvePreferredNoteDirectoryPath(normalizedNoteId, noteDirectoryPath);
    if (resolvedDirectoryPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to resolve note directory path.");
        }
        return false;
    }

    m_boundNoteId = normalizedNoteId;
    m_boundNoteDirectoryPath = resolvedDirectoryPath;
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

void ContentsNoteManagementCoordinator::resetBoundNotePersistenceSession()
{
    m_boundNoteId.clear();
    m_boundNoteDirectoryPath.clear();
}

void ContentsNoteManagementCoordinator::refreshContentPersistenceState()
{
    const bool nextDirectPersistenceAvailable =
        hasInvokableMethod(m_contentController, kNoteDirectoryPathForNoteIdSignature)
        && hasInvokableMethod(m_contentController, kApplyPersistedBodyStateForNoteSignature);
    const bool nextFallbackPersistenceAvailable =
        hasInvokableMethod(m_contentController, kSaveBodyTextForNoteSignature)
        || hasInvokableMethod(m_contentController, kSaveCurrentBodyTextSignature);
    const bool nextContractAvailable =
        nextDirectPersistenceAvailable || nextFallbackPersistenceAvailable;

    const bool changed =
        m_contentPersistenceContractAvailable != nextContractAvailable
        || m_directPersistenceContractAvailable != nextDirectPersistenceAvailable;

    m_contentPersistenceContractAvailable = nextContractAvailable;
    m_directPersistenceContractAvailable = nextDirectPersistenceAvailable;

    if (changed)
    {
        emit contentPersistenceContractAvailableChanged();
    }
}

bool ContentsNoteManagementCoordinator::enqueuePersistenceRequest(
    const QString& noteId,
    const QString& text)
{
    Request request;
    request.sequence = m_nextRequestSequence++;
    request.noteId = noteId.trimmed();
    request.text = text;

    if (m_directPersistenceContractAvailable)
    {
        QString sessionError;
        if (!ensureBoundNotePersistenceSession(request.noteId, QString(), &sessionError))
        {
            ThinkingSpace::Debug::traceSelf(
                this,
                QStringLiteral("content.note.management"),
                QStringLiteral("enqueuePersistenceRequest.failed"),
                QStringLiteral("noteId=%1 error=%2").arg(request.noteId, sessionError));
            return false;
        }
        request.kind = RequestKind::DirectPersistBody;
        request.noteDirectoryPath = m_boundNoteDirectoryPath;
        return enqueueRequest(std::move(request));
    }

    request.kind = RequestKind::ControllerPersistBody;
    request.noteDirectoryPath = resolveNoteDirectoryPathForNote(request.noteId);
    return enqueueRequest(std::move(request));
}

bool ContentsNoteManagementCoordinator::enqueueOpenCountIncrement(
    const QString& noteId,
    const QString& noteDirectoryPath)
{
    const QString normalizedNoteId = noteId.trimmed();
    const QString normalizedDirectoryPath = noteDirectoryPath.trimmed();
    if (normalizedNoteId.isEmpty() || normalizedDirectoryPath.isEmpty())
    {
        return false;
    }

    Request request;
    request.kind = RequestKind::IncrementOpenCount;
    request.sequence = m_nextRequestSequence++;
    request.noteId = normalizedNoteId;
    request.noteDirectoryPath = normalizedDirectoryPath;
    return enqueueRequest(std::move(request));
}

bool ContentsNoteManagementCoordinator::enqueueTrackedStatisticsRefresh(
    const QString& noteId,
    const QString& noteDirectoryPath,
    const bool incrementOpenCount)
{
    const QString normalizedNoteId = noteId.trimmed();
    const QString normalizedDirectoryPath = noteDirectoryPath.trimmed();
    if (normalizedNoteId.isEmpty() || normalizedDirectoryPath.isEmpty())
    {
        return false;
    }

    Request request;
    request.kind = RequestKind::RefreshTrackedStatistics;
    request.sequence = m_nextRequestSequence++;
    request.noteId = normalizedNoteId;
    request.noteDirectoryPath = normalizedDirectoryPath;
    request.incrementOpenCount = incrementOpenCount;
    return enqueueRequest(std::move(request));
}

bool ContentsNoteManagementCoordinator::enqueueRequest(Request request)
{
    if (request.noteId.isEmpty())
    {
        return false;
    }

    if (m_requestInFlight
        && m_activeRequest.kind == request.kind
        && m_activeRequest.noteId == request.noteId)
    {
        if ((request.kind == RequestKind::DirectPersistBody
            || request.kind == RequestKind::ControllerPersistBody
            || request.kind == RequestKind::ReconcileViewSessionSnapshot)
            && m_activeRequest.text == request.text
            && m_activeRequest.baseBodySourceText == request.baseBodySourceText
            && m_activeRequest.hasBaseBodySourceText == request.hasBaseBodySourceText)
        {
            if (request.kind != RequestKind::ReconcileViewSessionSnapshot
                || !request.preferViewSessionOnMismatch
                || m_activeRequest.preferViewSessionOnMismatch)
            {
                return true;
            }
        }
        if (request.kind == RequestKind::IncrementOpenCount
            || request.kind == RequestKind::RefreshTrackedStatistics)
        {
            return true;
        }
    }

    const int pendingIndex = findPendingRequestIndex(request.kind, request.noteId);
    if (pendingIndex >= 0)
    {
        Request& pendingRequest = m_pendingRequests[pendingIndex];
        if (request.kind == RequestKind::DirectPersistBody
            || request.kind == RequestKind::ControllerPersistBody)
        {
            if (pendingRequest.text == request.text
                && pendingRequest.baseBodySourceText == request.baseBodySourceText
                && pendingRequest.hasBaseBodySourceText == request.hasBaseBodySourceText)
            {
                return true;
            }
            pendingRequest = std::move(request);
            return true;
        }
        if (request.kind == RequestKind::ReconcileViewSessionSnapshot)
        {
            if (pendingRequest.text == request.text)
            {
                pendingRequest.preferViewSessionOnMismatch =
                    pendingRequest.preferViewSessionOnMismatch
                    || request.preferViewSessionOnMismatch;
                return true;
            }
            pendingRequest = std::move(request);
            return true;
        }
        if (request.kind == RequestKind::RefreshTrackedStatistics)
        {
            pendingRequest.incrementOpenCount =
                pendingRequest.incrementOpenCount || request.incrementOpenCount;
            return true;
        }
        if (request.kind == RequestKind::LoadNoteBodyText)
        {
            pendingRequest = std::move(request);
            return true;
        }
        return true;
    }

    if (m_requestInFlight)
    {
        m_pendingRequests.push_back(std::move(request));
        return true;
    }

    m_activeRequest = std::move(request);
    m_requestInFlight = true;
    dispatchNextRequest();
    return true;
}

int ContentsNoteManagementCoordinator::findPendingRequestIndex(
    const RequestKind kind,
    const QString& noteId) const
{
    const QString normalizedNoteId = noteId.trimmed();
    for (int index = m_pendingRequests.size() - 1; index >= 0; --index)
    {
        const Request& pendingRequest = m_pendingRequests.at(index);
        if (pendingRequest.kind == kind && pendingRequest.noteId == normalizedNoteId)
        {
            return index;
        }
    }
    return -1;
}

void ContentsNoteManagementCoordinator::dispatchNextRequest()
{
    if (!m_requestInFlight)
    {
        if (m_pendingRequests.isEmpty())
        {
            return;
        }
        m_activeRequest = m_pendingRequests.takeFirst();
        m_requestInFlight = true;
    }

    const Request request = m_activeRequest;
    if (request.kind == RequestKind::ControllerPersistBody)
    {
        dispatchControllerPersistenceRequest(request);
        return;
    }

    QObject* deliveryTarget = QCoreApplication::instance();
    if (deliveryTarget == nullptr)
    {
        deliveryTarget = this;
    }

    QPointer<ContentsNoteManagementCoordinator> coordinatorGuard(this);
    QThreadPool::globalInstance()->start([coordinatorGuard, deliveryTarget, request]()
    {
        const Result result = ContentsNoteManagementCoordinator::performWorkerRequest(request);
        QMetaObject::invokeMethod(
            deliveryTarget,
            [coordinatorGuard, result]()
            {
                if (coordinatorGuard != nullptr)
                {
                    coordinatorGuard->handleRequestFinished(result);
                }
            },
            Qt::QueuedConnection);
    });
}

void ContentsNoteManagementCoordinator::dispatchControllerPersistenceRequest(const Request& request)
{
    QPointer<ContentsNoteManagementCoordinator> coordinatorGuard(this);
    QMetaObject::invokeMethod(
        this,
        [coordinatorGuard, request]()
        {
            if (coordinatorGuard == nullptr)
            {
                return;
            }
            const Result result = coordinatorGuard->performControllerPersistence(request);
            coordinatorGuard->handleRequestFinished(result);
        },
        Qt::QueuedConnection);
}

ContentsNoteManagementCoordinator::Result
ContentsNoteManagementCoordinator::performWorkerRequest(const Request& request)
{
    Result result;
    result.kind = request.kind;
    result.sequence = request.sequence;
    result.noteId = request.noteId;
    result.noteDirectoryPath = request.noteDirectoryPath;
    result.text = request.text;
    result.lastModifiedAt = request.incomingLastModifiedAt;
    result.incrementOpenCount = request.incrementOpenCount;

    if (request.kind == RequestKind::LoadNoteBodyText)
    {
        ThinkingSpaceLocalNoteFileStore noteFileStore;
        ThinkingSpaceLocalNoteDocument document;
        ThinkingSpaceLocalNoteFileStore::ReadRequest readRequest;
        readRequest.noteId = request.noteId;
        readRequest.noteDirectoryPath = request.noteDirectoryPath;
        if (!noteFileStore.readNote(readRequest, &document, &result.errorMessage))
        {
            return result;
        }

        // The selection/mount stack needs the canonical persisted source here so the parser-backed
        // document host can preserve structural tags instead of silently flattening them away.
        result.text = document.bodySourceText.isEmpty() ? document.bodyPlainText : document.bodySourceText;
        result.lastModifiedAt = document.headerStore.lastModifiedAt();
        result.success = true;
        return result;
    }

    if (request.kind == RequestKind::DirectPersistBody)
    {
        ThinkingSpaceLocalNoteFileStore noteFileStore;
        ThinkingSpaceLocalNoteDocument document;
        ThinkingSpaceLocalNoteFileStore::ReadRequest readRequest;
        readRequest.noteId = request.noteId;
        readRequest.noteDirectoryPath = request.noteDirectoryPath;
        if (!noteFileStore.readNote(readRequest, &document, &result.errorMessage))
        {
            return result;
        }

        const QString filesystemBodySourceText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(
            document.bodySourceText.isEmpty() ? document.bodyPlainText : document.bodySourceText);
        const QString incomingBodySourceText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(request.text);
        const QString baseBodySourceText = request.hasBaseBodySourceText
            ? ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(request.baseBodySourceText)
            : filesystemBodySourceText;

        ThinkingSpaceNoteVersionDiffBuilder diffBuilder;
        const ThinkingSpaceNoteVersionDiffSegment bodyDiff = diffBuilder.diffSegment(
            baseBodySourceText,
            incomingBodySourceText,
            QStringLiteral("body.tsnbody"));
        bool diffApplied = false;
        QString diffApplyError;
        const QString mergedBodySourceText = diffBuilder.applyDiffSegmentOntoCurrent(
            baseBodySourceText,
            filesystemBodySourceText,
            bodyDiff,
            &diffApplied,
            &diffApplyError);
        if (!diffApplied)
        {
            result.errorMessage = diffApplyError.trimmed().isEmpty()
                ? QStringLiteral("Failed to apply editor body diff.")
                : diffApplyError.trimmed();
            return result;
        }

        ThinkingSpaceLocalNoteFileStore::UpdateRequest updateRequest;
        updateRequest.document = std::move(document);
        updateRequest.document.bodyPlainText = mergedBodySourceText;
        updateRequest.document.bodySourceText = mergedBodySourceText;
        updateRequest.persistHeader = false;
        updateRequest.persistBody = true;
        updateRequest.touchLastModified = true;
        updateRequest.incrementModifiedCount = true;
        updateRequest.baseLastModifiedAt = request.baseLastModifiedAt;
        updateRequest.incomingLastModifiedAt = request.incomingLastModifiedAt;
        updateRequest.resolveTimestampConflicts = false;
        updateRequest.refreshIncomingBacklinkStatistics = false;
        updateRequest.refreshAffectedBacklinkTargets = false;

        if (!noteFileStore.updateNote(
                updateRequest,
                &result.persistedDocument,
                &result.errorMessage,
                &result.updateResult))
        {
            return result;
        }

        result.text = result.persistedDocument.effectiveBodyText();
        result.success = true;
        return result;
    }

    if (request.kind == RequestKind::ReconcileViewSessionSnapshot)
    {
        ThinkingSpaceLocalNoteFileStore noteFileStore;
        ThinkingSpaceLocalNoteFileStore::ReadRequest readRequest;
        readRequest.noteId = request.noteId;
        readRequest.noteDirectoryPath = request.noteDirectoryPath;

        ThinkingSpaceLocalNoteDocument document;
        if (!noteFileStore.readNote(readRequest, &document, &result.errorMessage))
        {
            return result;
        }

        const QString normalizedRawSourceText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(
            document.bodySourceText.isEmpty() ? document.bodyPlainText : document.bodySourceText);
        const bool mismatchDetected = request.text != normalizedRawSourceText;
        if (!mismatchDetected)
        {
            result.success = true;
            return result;
        }

        if (!request.preferViewSessionOnMismatch)
        {
            result.snapshotRefreshRequested = true;
            result.success = true;
            return result;
        }

        ThinkingSpaceNoteVersionDiffBuilder diffBuilder;
        const ThinkingSpaceNoteVersionDiffSegment viewSessionDiff = diffBuilder.diffSegment(
            normalizedRawSourceText,
            request.text,
            QStringLiteral("body.tsnbody"));
        if (isWholeDocumentReplacementDiff(normalizedRawSourceText, viewSessionDiff))
        {
            result.snapshotRefreshRequested = true;
            result.success = true;
            return result;
        }

        bool diffApplied = false;
        QString diffApplyError;
        const QString reconciledBodySourceText = diffBuilder.applyDiffSegmentOntoCurrent(
            normalizedRawSourceText,
            normalizedRawSourceText,
            viewSessionDiff,
            &diffApplied,
            &diffApplyError);
        if (!diffApplied)
        {
            result.snapshotRefreshRequested = true;
            result.success = true;
            return result;
        }

        ThinkingSpaceLocalNoteFileStore::UpdateRequest updateRequest;
        updateRequest.document = std::move(document);
        updateRequest.document.bodyPlainText = reconciledBodySourceText;
        updateRequest.document.bodySourceText = reconciledBodySourceText;
        updateRequest.persistHeader = false;
        updateRequest.persistBody = true;
        updateRequest.touchLastModified = true;
        updateRequest.incrementModifiedCount = true;
        updateRequest.resolveTimestampConflicts = false;
        updateRequest.refreshIncomingBacklinkStatistics = false;
        updateRequest.refreshAffectedBacklinkTargets = false;

        if (!noteFileStore.updateNote(
                updateRequest,
                &result.persistedDocument,
                &result.errorMessage,
                &result.updateResult))
        {
            return result;
        }

        result.viewSessionPersisted = true;
        result.snapshotRefreshRequested = true;
        result.success = true;
        return result;
    }

    if (request.kind == RequestKind::IncrementOpenCount)
    {
        result.success = ThinkingSpace::NoteFileStatSupport::incrementOpenCountForNoteHeader(
            request.noteId,
            request.noteDirectoryPath,
            &result.errorMessage);
        return result;
    }

    if (request.kind == RequestKind::RefreshTrackedStatistics)
    {
        result.success = ThinkingSpace::NoteFileStatSupport::refreshTrackedStatisticsForNote(
            request.noteId,
            request.noteDirectoryPath,
            request.incrementOpenCount,
            &result.errorMessage);
        return result;
    }

    result.errorMessage = QStringLiteral("Unsupported worker request.");
    return result;
}

ContentsNoteManagementCoordinator::Result
ContentsNoteManagementCoordinator::performControllerPersistence(const Request& request) const
{
    Result result;
    result.kind = request.kind;
    result.sequence = request.sequence;
    result.noteId = request.noteId;
    result.noteDirectoryPath = request.noteDirectoryPath;
    result.text = request.text;

    if (m_contentController == nullptr)
    {
        result.errorMessage = QStringLiteral("contentController is not available.");
        return result;
    }

    bool saved = false;
    bool invoked = false;
    if (hasInvokableMethod(m_contentController, kSaveBodyTextForNoteSignature))
    {
        invoked = QMetaObject::invokeMethod(
            m_contentController,
            "saveBodyTextForNote",
            Qt::DirectConnection,
            Q_RETURN_ARG(bool, saved),
            Q_ARG(QString, result.noteId),
            Q_ARG(QString, result.text));
    }
    else if (hasInvokableMethod(m_contentController, kSaveCurrentBodyTextSignature))
    {
        invoked = QMetaObject::invokeMethod(
            m_contentController,
            "saveCurrentBodyText",
            Qt::DirectConnection,
            Q_RETURN_ARG(bool, saved),
            Q_ARG(QString, result.text));
    }

    result.success = invoked && saved;
    if (!result.success)
    {
        result.errorMessage = QStringLiteral("Fallback content persistence contract rejected the request.");
        return result;
    }

    if (result.noteDirectoryPath.isEmpty())
    {
        result.noteDirectoryPath = resolveNoteDirectoryPathForNote(result.noteId);
    }
    return result;
}

void ContentsNoteManagementCoordinator::handleRequestFinished(const Result& result)
{
    if (result.kind == RequestKind::LoadNoteBodyText)
    {
        if (!result.success)
        {
            ThinkingSpace::Debug::traceSelf(
                this,
                QStringLiteral("content.note.management"),
                QStringLiteral("loadNoteBodyText.failed"),
                QStringLiteral("noteId=%1 error=%2").arg(result.noteId, result.errorMessage));
        }

        emit noteBodyTextLoaded(
            result.sequence,
            result.noteId,
            result.text,
            result.success,
            result.errorMessage,
            result.lastModifiedAt);
    }
    else if (result.kind == RequestKind::DirectPersistBody)
    {
        bool success = result.success;
        QString errorMessage = result.errorMessage;
        const QString syncNotePath =
            !result.persistedDocument.noteDirectoryPath.trimmed().isEmpty()
                ? result.persistedDocument.noteDirectoryPath.trimmed()
                : result.noteDirectoryPath.trimmed();

        if (success && !syncMountedNotePathToSource(syncNotePath, &errorMessage))
        {
            success = false;
            ThinkingSpace::Debug::traceSelf(
                this,
                QStringLiteral("content.note.management"),
                QStringLiteral("persistBody.sourceSyncFailed"),
                QStringLiteral("noteId=%1 path=%2 error=%3").arg(result.noteId, syncNotePath, errorMessage));
        }

        if (success)
        {
            const QString persistedDirectoryPath = result.persistedDocument.noteDirectoryPath.trimmed();
            if (m_boundNoteId == result.noteId
                && !persistedDirectoryPath.isEmpty())
            {
                m_boundNoteDirectoryPath = persistedDirectoryPath;
            }

            const bool appliedToContentController = applyPersistedBodyStateToContentController(
                result.noteId,
                result.persistedDocument);
            if (!appliedToContentController)
            {
                reloadNoteMetadataForNote(result.noteId);
                if (m_contentController != nullptr
                    && hasInvokableMethod(m_contentController, kRequestControllerHookSignature))
                {
                    QMetaObject::invokeMethod(
                        m_contentController,
                        "requestControllerHook",
                        Qt::QueuedConnection);
                }
            }

            const QString statsDirectoryPath =
                !persistedDirectoryPath.isEmpty() ? persistedDirectoryPath : resolveNoteDirectoryPathForNote(result.noteId);
            enqueueTrackedStatisticsRefresh(result.noteId, statsDirectoryPath, false);

            if (result.updateResult.versionDiffPushedToFilesystem)
            {
                emit hubFilesystemMutated();
            }
        }
        else
        {
            ThinkingSpace::Debug::traceSelf(
                this,
                QStringLiteral("content.note.management"),
                QStringLiteral("persistBody.failed"),
                QStringLiteral("noteId=%1 error=%2").arg(result.noteId, errorMessage));
        }

        emit editorTextPersistenceFinished(
            result.noteId,
            result.text,
            success,
            errorMessage);
    }
    else if (result.kind == RequestKind::ControllerPersistBody)
    {
        bool success = result.success;
        QString errorMessage = result.errorMessage;

        if (success && !syncMountedNotePathToSource(result.noteDirectoryPath, &errorMessage))
        {
            success = false;
            ThinkingSpace::Debug::traceSelf(
                this,
                QStringLiteral("content.note.management"),
                QStringLiteral("fallbackPersistBody.sourceSyncFailed"),
                QStringLiteral("noteId=%1 path=%2 error=%3").arg(result.noteId, result.noteDirectoryPath, errorMessage));
        }

        if (success)
        {
            const QString statsDirectoryPath =
                !result.noteDirectoryPath.isEmpty()
                    ? result.noteDirectoryPath
                    : resolveNoteDirectoryPathForNote(result.noteId);
            enqueueTrackedStatisticsRefresh(result.noteId, statsDirectoryPath, false);
        }
        else
        {
            ThinkingSpace::Debug::traceSelf(
                this,
                QStringLiteral("content.note.management"),
                QStringLiteral("fallbackPersistBody.failed"),
                QStringLiteral("noteId=%1 error=%2").arg(result.noteId, errorMessage));
        }

        emit editorTextPersistenceFinished(
            result.noteId,
            result.text,
            success,
            errorMessage);
    }
    else if (result.kind == RequestKind::IncrementOpenCount)
    {
        bool success = result.success;
        QString errorMessage = result.errorMessage;

        if (success && !syncMountedNotePathToSource(result.noteDirectoryPath, &errorMessage))
        {
            success = false;
        }

        if (!success)
        {
            ThinkingSpace::Debug::traceSelf(
                this,
                QStringLiteral("content.note.management"),
                QStringLiteral("incrementOpenCount.failed"),
                QStringLiteral("noteId=%1 error=%2").arg(result.noteId, errorMessage));
        }
        else
        {
            reloadNoteMetadataForNote(result.noteId);
        }
    }
    else if (result.kind == RequestKind::RefreshTrackedStatistics)
    {
        bool success = result.success;
        QString errorMessage = result.errorMessage;

        if (success && !syncMountedNotePathToSource(result.noteDirectoryPath, &errorMessage))
        {
            success = false;
        }

        if (success)
        {
            reloadNoteMetadataForNote(result.noteId);
        }
        else
        {
            ThinkingSpace::Debug::traceSelf(
                this,
                QStringLiteral("content.note.management"),
                QStringLiteral("refreshTrackedStatistics.failed"),
                QStringLiteral("noteId=%1 error=%2").arg(result.noteId, errorMessage));
        }
    }
    else if (result.kind == RequestKind::ReconcileViewSessionSnapshot)
    {
        bool refreshed = false;
        bool success = result.success;
        QString errorMessage = result.errorMessage;

        if (success && result.viewSessionPersisted)
        {
            const QString syncNotePath =
                !result.persistedDocument.noteDirectoryPath.trimmed().isEmpty()
                    ? result.persistedDocument.noteDirectoryPath.trimmed()
                    : result.noteDirectoryPath.trimmed();

            if (!syncMountedNotePathToSource(syncNotePath, &errorMessage))
            {
                success = false;
                ThinkingSpace::Debug::traceSelf(
                    this,
                    QStringLiteral("content.note.management"),
                    QStringLiteral("reconcileViewSession.sourceSyncFailed"),
                    QStringLiteral("noteId=%1 path=%2 error=%3").arg(result.noteId, syncNotePath, errorMessage));
            }

            if (success)
            {
                const QString persistedDirectoryPath = result.persistedDocument.noteDirectoryPath.trimmed();
                if (m_boundNoteId == result.noteId
                    && !persistedDirectoryPath.isEmpty())
                {
                    m_boundNoteDirectoryPath = persistedDirectoryPath;
                }

                const bool appliedToContentController = applyPersistedBodyStateToContentController(
                    result.noteId,
                    result.persistedDocument);
                if (!appliedToContentController)
                {
                    reloadNoteMetadataForNote(result.noteId);
                    if (m_contentController != nullptr
                        && hasInvokableMethod(m_contentController, kRequestControllerHookSignature))
                    {
                        QMetaObject::invokeMethod(
                            m_contentController,
                            "requestControllerHook",
                            Qt::QueuedConnection);
                    }
                }

                const QString statsDirectoryPath =
                    !persistedDirectoryPath.isEmpty()
                        ? persistedDirectoryPath
                        : resolveNoteDirectoryPathForNote(result.noteId);
                enqueueTrackedStatisticsRefresh(result.noteId, statsDirectoryPath, false);

                if (result.updateResult.versionDiffPushedToFilesystem)
                {
                    emit hubFilesystemMutated();
                }
            }
        }

        if (success && result.snapshotRefreshRequested)
        {
            refreshed = refreshNoteSnapshotForNote(result.noteId);
            if (!refreshed)
            {
                success = false;
                if (errorMessage.isEmpty())
                {
                    errorMessage = QStringLiteral("Failed to refresh note snapshot after reconcile.");
                }
            }
        }

        if (!success)
        {
            ThinkingSpace::Debug::traceSelf(
                this,
                QStringLiteral("content.note.management"),
                QStringLiteral("reconcileViewSession.failed"),
                QStringLiteral("noteId=%1 error=%2").arg(result.noteId, errorMessage));
        }

        emit viewSessionSnapshotReconciled(
            result.noteId,
            refreshed,
            success,
            errorMessage);
    }

    if (!m_pendingRequests.isEmpty())
    {
        m_activeRequest = m_pendingRequests.takeFirst();
        m_requestInFlight = true;
        dispatchNextRequest();
        return;
    }

    m_activeRequest = Request();
    m_requestInFlight = false;
}

bool ContentsNoteManagementCoordinator::applyPersistedBodyStateToContentController(
    const QString& noteId,
    const ThinkingSpaceLocalNoteDocument& document) const
{
    if (m_contentController == nullptr
        || !hasInvokableMethod(m_contentController, kApplyPersistedBodyStateForNoteSignature))
    {
        return false;
    }

    bool applied = false;
    return QMetaObject::invokeMethod(
               m_contentController,
               "applyPersistedBodyStateForNote",
               Qt::DirectConnection,
               Q_RETURN_ARG(bool, applied),
               Q_ARG(QString, noteId.trimmed()),
               Q_ARG(QString, document.bodyPlainText),
               Q_ARG(QString, document.bodySourceText),
               Q_ARG(QString, document.headerStore.lastModifiedAt()))
        && applied;
}

bool ContentsNoteManagementCoordinator::reloadNoteMetadataForNote(const QString& noteId) const
{
    const QString normalizedNoteId = noteId.trimmed();
    if (normalizedNoteId.isEmpty()
        || m_contentController == nullptr
        || !hasInvokableMethod(m_contentController, kReloadNoteMetadataForNoteIdSignature))
    {
        return false;
    }

    bool reloaded = false;
    if (!QMetaObject::invokeMethod(
            m_contentController,
            "reloadNoteMetadataForNoteId",
            Qt::DirectConnection,
            Q_RETURN_ARG(bool, reloaded),
            Q_ARG(QString, normalizedNoteId))
        || !reloaded)
    {
        ThinkingSpace::Debug::traceSelf(
            this,
            QStringLiteral("content.note.management"),
            QStringLiteral("reloadNoteMetadata.failed"),
            QStringLiteral("noteId=%1").arg(normalizedNoteId));
        return false;
    }

    return true;
}
