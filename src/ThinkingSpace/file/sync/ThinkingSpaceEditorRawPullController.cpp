#include "ThinkingSpace/file/sync/ThinkingSpaceEditorRawPullController.hpp"

#include <QDir>

#include <algorithm>
#include <utility>

namespace
{
    constexpr auto kNoteEntryReason = "note-entry";
    constexpr auto kNoteOpenReason = "note-open";
    constexpr auto kIdleReason = "idle";
}

ThinkingSpaceEditorRawPullController::ThinkingSpaceEditorRawPullController(QObject* parent)
    : QObject(parent)
{
    m_idlePullTimer.setSingleShot(true);
    m_idlePullTimer.setInterval(m_idlePullIntervalMs);
    connect(
        &m_idlePullTimer,
        &QTimer::timeout,
        this,
        &ThinkingSpaceEditorRawPullController::requestActiveIdlePull);
}

int ThinkingSpaceEditorRawPullController::idlePullIntervalMs() const noexcept
{
    return m_idlePullIntervalMs;
}

void ThinkingSpaceEditorRawPullController::setIdlePullIntervalMs(const int idlePullIntervalMs)
{
    const int normalizedInterval = std::max(0, idlePullIntervalMs);
    if (m_idlePullIntervalMs == normalizedInterval)
    {
        return;
    }

    m_idlePullIntervalMs = normalizedInterval;
    m_idlePullTimer.setInterval(m_idlePullIntervalMs);
    if (!m_activeIdlePullNoteId.isEmpty()
        && !m_activeIdlePullNoteDirectoryPath.isEmpty()
        && m_idlePullTimer.isActive())
    {
        m_idlePullTimer.start(m_idlePullIntervalMs);
    }
    emit idlePullIntervalMsChanged();
}

void ThinkingSpaceEditorRawPullController::setRawPullCallback(RawPullCallback callback)
{
    m_rawPullCallback = std::move(callback);
}

quint64 ThinkingSpaceEditorRawPullController::requestNoteEntryPull(
    const QString& noteId,
    const QString& noteDirectoryPath)
{
    return executePull(
        normalizedNoteId(noteId),
        normalizedPath(noteDirectoryPath),
        QString::fromLatin1(kNoteEntryReason));
}

quint64 ThinkingSpaceEditorRawPullController::requestNoteOpenPull(
    const QString& noteId,
    const QString& noteDirectoryPath)
{
    return executePull(
        normalizedNoteId(noteId),
        normalizedPath(noteDirectoryPath),
        QString::fromLatin1(kNoteOpenReason));
}

void ThinkingSpaceEditorRawPullController::setActiveNoteForIdlePull(
    const QString& noteId,
    const QString& noteDirectoryPath)
{
    const QString normalizedId = normalizedNoteId(noteId);
    const QString normalizedDirectoryPath = normalizedPath(noteDirectoryPath);
    if (normalizedId.isEmpty() || normalizedDirectoryPath.isEmpty())
    {
        clearActiveNoteForIdlePull();
        return;
    }

    const bool sameActiveNote =
        m_activeIdlePullNoteId == normalizedId
        && m_activeIdlePullNoteDirectoryPath == normalizedDirectoryPath;
    m_activeIdlePullNoteId = normalizedId;
    m_activeIdlePullNoteDirectoryPath = normalizedDirectoryPath;
    if (!sameActiveNote || !m_idlePullTimer.isActive())
    {
        scheduleIdlePull();
    }
}

void ThinkingSpaceEditorRawPullController::clearActiveNoteForIdlePull()
{
    m_idlePullTimer.stop();
    m_activeIdlePullNoteId.clear();
    m_activeIdlePullNoteDirectoryPath.clear();
}

void ThinkingSpaceEditorRawPullController::recordUserActivity()
{
    scheduleIdlePull();
}

quint64 ThinkingSpaceEditorRawPullController::requestActiveIdlePull()
{
    const QString noteId = m_activeIdlePullNoteId;
    const QString noteDirectoryPath = m_activeIdlePullNoteDirectoryPath;
    if (noteId.isEmpty() || noteDirectoryPath.isEmpty())
    {
        return 0;
    }

    const quint64 sequence = executePull(
        noteId,
        noteDirectoryPath,
        QString::fromLatin1(kIdleReason));
    if (m_activeIdlePullNoteId == noteId
        && m_activeIdlePullNoteDirectoryPath == noteDirectoryPath)
    {
        scheduleIdlePull();
    }
    return sequence;
}

QString ThinkingSpaceEditorRawPullController::normalizedNoteId(const QString& noteId)
{
    return noteId.trimmed();
}

QString ThinkingSpaceEditorRawPullController::normalizedPath(const QString& path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty())
    {
        return {};
    }
    const QString cleanPath = QDir::cleanPath(trimmedPath);
    return cleanPath == QStringLiteral(".") ? QString() : cleanPath;
}

quint64 ThinkingSpaceEditorRawPullController::executePull(
    const QString& noteId,
    const QString& noteDirectoryPath,
    const QString& reason)
{
    if (noteId.isEmpty() || noteDirectoryPath.isEmpty())
    {
        return 0;
    }

    emit rawPullRequested(noteId, noteDirectoryPath, reason);

    QString errorMessage;
    const quint64 sequence = m_rawPullCallback
        ? m_rawPullCallback(noteId, noteDirectoryPath, reason, &errorMessage)
        : 0;
    const bool success = sequence != 0;
    if (!success && errorMessage.trimmed().isEmpty())
    {
        errorMessage = QStringLiteral("Editor RAW pull callback is not available.");
    }

    emit rawPullFinished(
        noteId,
        noteDirectoryPath,
        reason,
        sequence,
        success,
        errorMessage);
    return sequence;
}

void ThinkingSpaceEditorRawPullController::scheduleIdlePull()
{
    if (m_activeIdlePullNoteId.isEmpty()
        || m_activeIdlePullNoteDirectoryPath.isEmpty())
    {
        return;
    }

    m_idlePullTimer.start(m_idlePullIntervalMs);
}
