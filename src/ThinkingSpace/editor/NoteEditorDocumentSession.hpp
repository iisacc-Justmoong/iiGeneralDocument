#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include "ThinkingSpace/file/note/session/ContentsNoteManagementCoordinator.hpp"
#include "ThinkingSpace/file/sync/ThinkingSpaceEditorRawPullController.hpp"
#include "ThinkingSpace/file/sync/ThinkingSpaceEditorRawPushController.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtTypes>

class NoteActiveStateTracker;

class NoteEditorDocumentSession final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* noteActiveState READ noteActiveState WRITE setNoteActiveState NOTIFY noteActiveStateChanged)
    Q_PROPERTY(QString editorFilePath READ editorFilePath NOTIFY editorFilePathChanged)
    Q_PROPERTY(QString activeNoteId READ activeNoteId NOTIFY activeNoteChanged)
    Q_PROPERTY(QString activeNoteDirectoryPath READ activeNoteDirectoryPath NOTIFY activeNoteChanged)
    Q_PROPERTY(int parsedLineCount READ parsedLineCount NOTIFY parsedLineCountChanged)
    Q_PROPERTY(int editorViewportWidth READ editorViewportWidth WRITE setEditorViewportWidth NOTIFY editorViewportWidthChanged)
    Q_PROPERTY(bool hasActiveNote READ hasActiveNote NOTIFY activeNoteChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit NoteEditorDocumentSession(QObject* parent = nullptr);
    ~NoteEditorDocumentSession() override;

    QObject* noteActiveState() const noexcept;
    void setNoteActiveState(QObject* noteActiveState);

    QString editorFilePath() const;
    QString activeNoteId() const;
    QString activeNoteDirectoryPath() const;
    int parsedLineCount() const noexcept;
    int editorViewportWidth() const noexcept;
    bool hasActiveNote() const noexcept;
    bool loading() const noexcept;
    bool readOnly() const noexcept;
    QString lastError() const;

    void setSessionRootPathForTests(const QString& sessionRootPath);
    void setEditorViewportWidth(int editorViewportWidth);

    Q_INVOKABLE bool openNoteForEditing(
        const QString& noteId,
        const QString& noteDirectoryPath);
    Q_INVOKABLE bool clearEditor();
    Q_INVOKABLE bool persistEditorFile(const QString& editorFilePath);
    Q_INVOKABLE bool markEditorSessionFileReadyForRawPush(const QString& editorFilePath);
    Q_INVOKABLE void requestEditorIdleRawPush(
        const QString& editorFilePath,
        const QString& editorDocumentText);
    Q_INVOKABLE void requestEditorModifiedCountRawPush(
        const QString& editorFilePath,
        int modifiedCount,
        const QString& editorDocumentText);
    Q_INVOKABLE void recordEditorUserActivity();
    Q_INVOKABLE quint64 requestActiveNoteIdleRawPull();
    Q_INVOKABLE QVariantMap insertImportedResourcesIntoSource(
        const QString& editorDocumentText,
        int cursorPosition,
        int selectionLength,
        const QVariantList& importedEntries);
    Q_INVOKABLE QVariantMap reprojectResourceFramesForEditorWidth(
        const QString& editorDocumentText,
        int editorViewportWidth);
    Q_INVOKABLE QVariantMap insertFormatTagIntoSource(
        const QString& tagName,
        const QString& editorDocumentText,
        int cursorPosition,
        int selectionLength,
        const QString& selectedText = QString());
    Q_INVOKABLE QVariantMap insertStyleTagIntoSource(
        const QString& styleValue,
        const QString& editorDocumentText,
        int cursorPosition,
        int selectionLength,
        const QString& selectedText = QString());
    Q_INVOKABLE QVariantMap insertStyleFontTagIntoSource(
        const QString& fontFamily,
        const QString& editorDocumentText,
        int cursorPosition,
        int selectionLength,
        const QString& selectedText = QString());
    Q_INVOKABLE QVariantMap insertStyleFontSizeTagIntoSource(
        const QString& fontSize,
        const QString& editorDocumentText,
        int cursorPosition,
        int selectionLength,
        const QString& selectedText = QString());
    Q_INVOKABLE QVariantMap insertStyleBackgroundTagIntoSource(
        const QString& backgroundColor,
        const QString& editorDocumentText,
        int cursorPosition,
        int selectionLength,
        const QString& selectedText = QString());
    Q_INVOKABLE QVariantList highlightColorMenuItems() const;
    Q_INVOKABLE QVariantMap toolbarStyleContextAtCursor(
        const QString& editorDocumentText,
        int cursorPosition,
        int selectionLength = 0);
    Q_INVOKABLE QVariantMap handleCalloutBoundaryKeyInSource(
        const QString& editorDocumentText,
        int cursorPosition,
        int selectionLength,
        int key);
    Q_INVOKABLE QVariantMap handleStyleBoundaryKeyInSource(
        const QString& editorDocumentText,
        int cursorPosition,
        int selectionLength,
        int key);

signals:
    void noteActiveStateChanged();
    void editorFilePathChanged();
    void activeNoteChanged();
    void parsedLineCountChanged();
    void editorViewportWidthChanged();
    void loadingChanged();
    void readOnlyChanged();
    void lastErrorChanged();
    void editorSourceLoaded(
        const QString& noteId,
        const QString& editorFilePath);
    void editorSourcePersistRequested(
        const QString& noteId,
        const QString& editorFilePath);
    void editorSourcePersistFinished(
        const QString& noteId,
        bool success,
        const QString& errorMessage);
    void hubFilesystemMutated();
    void editorDocumentTextPulled(
        const QString& noteId,
        const QString& editorDocumentText);
    void editorFilesystemPullIgnored(
        const QString& noteId,
        const QString& reason);

private slots:
    void refreshFromActiveNoteState();
    void refreshContentController();
    void handleNoteBodyTextLoaded(
        quint64 sequence,
        const QString& noteId,
        const QString& text,
        bool success,
        const QString& errorMessage,
        const QString& lastModifiedAt);
    void handleEditorTextPersistenceFinished(
        const QString& noteId,
        const QString& text,
        bool success,
        const QString& errorMessage);
    void handleNoteActiveStateDestroyed();

private:
    struct EditorFileContext final
    {
        QString noteId;
        QString noteDirectoryPath;
        QString loadedLastModifiedAt;
        QString loadedBodySourceText;
        bool readyForRawPush = false;
    };

    QString sessionRootPath() const;
    QString blankEditorFilePath() const;
    QString editorFilePathForNote(
        const QString& noteId,
        const QString& noteDirectoryPath) const;

    bool ensureSessionRoot(QString* errorMessage = nullptr) const;
    bool writeEditorSourceFile(
        const QString& filePath,
        const QString& text,
        QString* errorMessage = nullptr) const;
    bool readEditorSourceFile(
        const QString& filePath,
        QString* outText,
        QString* errorMessage = nullptr) const;
    bool persistEditorDocumentText(
        const QString& editorFilePath,
        const QString& editorDocumentText);
    bool persistBodySourceTextForEditorFile(
        const QString& editorFilePath,
        const QString& bodySourceText);
    bool prepareBodySourceTextForRawPush(
        const QString& editorFilePath,
        const QString& editorDocumentText,
        const QString& reason,
        QString* bodySourceText);
    bool stageActiveSourceMutationForCurrentEditorFile(
        const QString& bodySourceText,
        const QString& editorDocumentText,
        QString* errorMessage = nullptr);
    bool pushActiveEditorBeforeNoteDeparture();
    bool applyLoadedBodyTextToEditorSession(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const QString& bodySourceText,
        const QString& loadedLastModifiedAt,
        bool bindSelectedNote,
        bool emitPulledDocumentText);
    void handleIdleNoteBodyTextLoaded(
        quint64 sequence,
        const QString& noteId,
        const QString& text,
        bool success,
        const QString& errorMessage,
        const QString& lastModifiedAt);
    QString bodySourceTextForEditorDocument(
        const QString& noteId,
        const QString& editorDocumentText) const;

    void setEditorFilePath(const QString& editorFilePath);
    void setActiveNoteContext(
        const QString& noteId,
        const QString& noteDirectoryPath);
    void setParsedLineCount(int parsedLineCount);
    void setLoading(bool loading);
    void setReadOnly(bool readOnly);
    void setLastError(const QString& lastError);
    void switchToBlankEditorFile();

    QPointer<NoteActiveStateTracker> m_noteActiveState;
    ContentsNoteManagementCoordinator m_noteManagementCoordinator;
    ThinkingSpaceEditorRawPullController m_rawPullController;
    ThinkingSpaceEditorRawPushController m_rawPushController;
    QHash<QString, EditorFileContext> m_editorFileContexts;
    QString m_sessionRootPathForTests;
    QString m_editorFilePath;
    QString m_activeNoteId;
    QString m_activeNoteDirectoryPath;
    QString m_pendingLoadNoteId;
    QString m_pendingLoadNoteDirectoryPath;
    QString m_pendingIdlePullNoteId;
    QString m_pendingIdlePullNoteDirectoryPath;
    QString m_activeBodySourceText;
    bool m_activeBodySourceDirtyForEditorSession = false;
    quint64 m_pendingLoadSequence = 0;
    quint64 m_pendingIdlePullSequence = 0;
    int m_parsedLineCount = 0;
    int m_editorViewportWidth = 0;
    bool m_loading = false;
    bool m_readOnly = true;
    QString m_lastError;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
