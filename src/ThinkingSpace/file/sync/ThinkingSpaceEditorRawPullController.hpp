#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtTypes>

#include <functional>

class ThinkingSpaceEditorRawPullController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int idlePullIntervalMs READ idlePullIntervalMs WRITE setIdlePullIntervalMs NOTIFY idlePullIntervalMsChanged)

public:
    using RawPullCallback = std::function<quint64(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const QString& reason,
        QString* errorMessage)>;

    explicit ThinkingSpaceEditorRawPullController(QObject* parent = nullptr);

    int idlePullIntervalMs() const noexcept;
    void setRawPullCallback(RawPullCallback callback);

    Q_INVOKABLE quint64 requestNoteEntryPull(
        const QString& noteId,
        const QString& noteDirectoryPath);
    Q_INVOKABLE quint64 requestNoteOpenPull(
        const QString& noteId,
        const QString& noteDirectoryPath);
    Q_INVOKABLE void setActiveNoteForIdlePull(
        const QString& noteId,
        const QString& noteDirectoryPath);
    Q_INVOKABLE void clearActiveNoteForIdlePull();
    Q_INVOKABLE void recordUserActivity();
    Q_INVOKABLE quint64 requestActiveIdlePull();

public slots:
    void setIdlePullIntervalMs(int idlePullIntervalMs);

signals:
    void idlePullIntervalMsChanged();
    void rawPullRequested(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const QString& reason);
    void rawPullFinished(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const QString& reason,
        quint64 sequence,
        bool success,
        const QString& errorMessage);

private:
    static QString normalizedNoteId(const QString& noteId);
    static QString normalizedPath(const QString& path);

    quint64 executePull(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const QString& reason);
    void scheduleIdlePull();

    RawPullCallback m_rawPullCallback;
    QTimer m_idlePullTimer;
    QString m_activeIdlePullNoteId;
    QString m_activeIdlePullNoteDirectoryPath;
    int m_idlePullIntervalMs = 5000;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
