#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteFileStore.hpp"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtTypes>
#include <QVector>

class ContentsNoteManagementCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit ContentsNoteManagementCoordinator(QObject* parent = nullptr);
    ~ContentsNoteManagementCoordinator() override;

    QObject* contentController() const noexcept;
    void setContentController(QObject* model);

    bool contentPersistenceContractAvailable() const noexcept;
    bool directPersistenceAvailable() const noexcept;

    bool persistEditorTextForNote(const QString& noteId, const QString& text);
    bool persistEditorTextForNoteAtPath(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const QString& text,
        const QString& baseLastModifiedAt = QString(),
        const QString& baseBodySourceText = QString(),
        bool hasBaseBodySourceText = false);
    bool captureDirectPersistenceContextForNote(
        const QString& noteId,
        QString* noteDirectoryPath) const;
    QString noteDirectoryPathForNote(const QString& noteId) const;
    quint64 loadNoteBodyTextForNote(
        const QString& noteId,
        const QString& noteDirectoryPath = QString());
    bool reconcileViewSessionAndRefreshSnapshotForNote(
        const QString& noteId,
        const QString& viewSessionText,
        bool preferViewSessionOnMismatch = false,
        const QString& noteDirectoryPath = QString());
    bool refreshNoteSnapshotForNote(const QString& noteId);
    void bindSelectedNote(const QString& noteId, const QString& noteDirectoryPath = QString());
    void clearSelectedNote();

signals:
    void contentPersistenceContractAvailableChanged();
    void editorTextPersistenceFinished(
        const QString& noteId,
        const QString& text,
        bool success,
        const QString& errorMessage);
    void hubFilesystemMutated();
    void noteBodyTextLoaded(
        quint64 sequence,
        const QString& noteId,
        const QString& text,
        bool success,
        const QString& errorMessage,
        const QString& lastModifiedAt);
    void viewSessionSnapshotReconciled(
        const QString& noteId,
        bool refreshed,
        bool success,
        const QString& errorMessage);

private slots:
    void handleContentControllerDestroyed();

private:
    enum class RequestKind
    {
        LoadNoteBodyText,
        DirectPersistBody,
        ControllerPersistBody,
        IncrementOpenCount,
        RefreshTrackedStatistics,
        ReconcileViewSessionSnapshot
    };

    struct Request final
    {
        RequestKind kind = RequestKind::DirectPersistBody;
        quint64 sequence = 0;
        QString noteId;
        QString noteDirectoryPath;
        QString text;
        QString baseBodySourceText;
        QString baseLastModifiedAt;
        QString incomingLastModifiedAt;
        bool incrementOpenCount = false;
        bool preferViewSessionOnMismatch = false;
        bool hasBaseBodySourceText = false;
    };

    struct Result final
    {
        RequestKind kind = RequestKind::DirectPersistBody;
        quint64 sequence = 0;
        QString noteId;
        QString noteDirectoryPath;
        QString text;
        QString lastModifiedAt;
        ThinkingSpaceLocalNoteDocument persistedDocument;
        ThinkingSpaceLocalNoteFileStore::UpdateResult updateResult;
        QString errorMessage;
        bool incrementOpenCount = false;
        bool snapshotRefreshRequested = false;
        bool viewSessionPersisted = false;
        bool success = false;
    };

    static bool hasInvokableMethod(const QObject* object, const char* methodSignature);

    QString resolveNoteDirectoryPathForNote(const QString& noteId) const;
    QString resolvePreferredNoteDirectoryPath(
        const QString& noteId,
        const QString& noteDirectoryPath = QString()) const;
    bool ensureBoundNotePersistenceSession(
        const QString& noteId,
        const QString& noteDirectoryPath = QString(),
        QString* errorMessage = nullptr);
    void resetBoundNotePersistenceSession();
    void refreshContentPersistenceState();

    bool enqueuePersistenceRequest(const QString& noteId, const QString& text);
    bool enqueueOpenCountIncrement(const QString& noteId, const QString& noteDirectoryPath);
    bool enqueueTrackedStatisticsRefresh(
        const QString& noteId,
        const QString& noteDirectoryPath,
        bool incrementOpenCount);
    bool enqueueRequest(Request request);
    int findPendingRequestIndex(RequestKind kind, const QString& noteId) const;
    void dispatchNextRequest();
    void dispatchControllerPersistenceRequest(const Request& request);
    static Result performWorkerRequest(const Request& request);
    Result performControllerPersistence(const Request& request) const;
    void handleRequestFinished(const Result& result);

    bool applyPersistedBodyStateToContentController(
        const QString& noteId,
        const ThinkingSpaceLocalNoteDocument& document) const;
    bool reloadNoteMetadataForNote(const QString& noteId) const;

    QPointer<QObject> m_contentController;
    bool m_contentPersistenceContractAvailable = false;
    bool m_directPersistenceContractAvailable = false;
    QString m_boundNoteId;
    QString m_boundNoteDirectoryPath;
    bool m_requestInFlight = false;
    quint64 m_nextRequestSequence = 1;
    Request m_activeRequest;
    QVector<Request> m_pendingRequests;
    QMetaObject::Connection m_contentControllerDestroyedConnection;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
