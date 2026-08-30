#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

class ThinkingSpaceEditorRawPushController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int idleIntervalMs READ idleIntervalMs WRITE setIdleIntervalMs NOTIFY idleIntervalMsChanged)

public:
    using RawPushCallback = std::function<bool(
        const QString& editorFilePath,
        const QString& bodySourceText,
        bool hasBodySourceText,
        const QString& reason,
        QString* errorMessage)>;

    explicit ThinkingSpaceEditorRawPushController(QObject* parent = nullptr);

    int idleIntervalMs() const noexcept;
    void setRawPushCallback(RawPushCallback callback);

    Q_INVOKABLE void requestIdlePush(
        const QString& editorFilePath,
        const QString& bodySourceText);
    Q_INVOKABLE void requestModifiedCountPush(
        const QString& editorFilePath,
        int modifiedCount,
        const QString& bodySourceText);
    Q_INVOKABLE bool pushBeforeNoteDeparture(const QString& editorFilePath);
    Q_INVOKABLE bool flushPendingPush();
    Q_INVOKABLE bool discardPendingPushForFile(const QString& editorFilePath);
    Q_INVOKABLE bool hasPendingPushForFile(const QString& editorFilePath) const;

public slots:
    void setIdleIntervalMs(int idleIntervalMs);

signals:
    void idleIntervalMsChanged();
    void rawPushRequested(
        const QString& editorFilePath,
        const QString& reason);
    void rawPushFinished(
        const QString& editorFilePath,
        const QString& reason,
        bool success,
        const QString& errorMessage);

private:
    struct PendingPush final
    {
        QString editorFilePath;
        QString bodySourceText;
        QString reason;
        bool hasBodySourceText = false;
    };

    static QString normalizedPath(const QString& path);

    void schedulePush(PendingPush push);
    bool executePush(const PendingPush& push);
    void resetModifiedCountTrackingIfNeeded(const QString& editorFilePath);

    RawPushCallback m_rawPushCallback;
    QTimer m_idleTimer;
    PendingPush m_pendingPush;
    QString m_modifiedCountFilePath;
    int m_idleIntervalMs = 250;
    int m_lastModifiedCount = -1;
    bool m_hasPendingPush = false;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
