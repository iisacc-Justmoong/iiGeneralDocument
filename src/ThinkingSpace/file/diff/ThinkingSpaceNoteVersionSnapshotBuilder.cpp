#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionSnapshotBuilder.hpp"

#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionDiffBuilder.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>

namespace
{
    QString idSafeLabel(QString label)
    {
        label = label.trimmed().toLower();
        label.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
        label.replace(QRegularExpression(QStringLiteral("^-+|-+$")), QString());
        return label.isEmpty() ? QStringLiteral("snapshot") : label;
    }

    QString uniqueSnapshotId(
        const ThinkingSpaceNoteVersionSnapshotBuilder& builder,
        const ThinkingSpaceNoteVersionState& state,
        const QString& label,
        const QString& headerText,
        const QString& bodyDocumentText)
    {
        QByteArray seed;
        seed += label.toUtf8();
        seed += '\0';
        seed += headerText.toUtf8();
        seed += '\0';
        seed += bodyDocumentText.toUtf8();
        const QString hash =
            QString::fromLatin1(QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex().left(16));
        const QString baseId = QStringLiteral("%1-%2").arg(idSafeLabel(label), hash);

        QString candidate = baseId;
        int suffix = 2;
        while (builder.findSnapshot(state, candidate) != nullptr)
        {
            candidate = QStringLiteral("%1-%2").arg(baseId, QString::number(suffix));
            ++suffix;
        }
        return candidate;
    }
} // namespace

const ThinkingSpaceNoteVersionSnapshot* ThinkingSpaceNoteVersionSnapshotBuilder::findSnapshot(
    const ThinkingSpaceNoteVersionState& state,
    const QString& snapshotId) const
{
    for (const ThinkingSpaceNoteVersionSnapshot& snapshot : state.snapshots)
    {
        if (snapshot.snapshotId == snapshotId)
        {
            return &snapshot;
        }
    }
    return nullptr;
}

QString ThinkingSpaceNoteVersionSnapshotBuilder::parentSnapshotIdForCapture(
    const ThinkingSpaceNoteVersionState& state) const
{
    if (findSnapshot(state, state.currentSnapshotId) != nullptr)
    {
        return state.currentSnapshotId;
    }
    if (findSnapshot(state, state.headSnapshotId) != nullptr)
    {
        return state.headSnapshotId;
    }
    return state.snapshots.isEmpty() ? QString() : state.snapshots.constLast().snapshotId;
}

ThinkingSpaceNoteVersionSnapshot ThinkingSpaceNoteVersionSnapshotBuilder::buildSnapshot(
    const ThinkingSpaceNoteVersionState& state,
    const QString& parentSnapshotId,
    const QString& sourceSnapshotId,
    const QString& label,
    const int commitModifiedCount,
    const QString& headerText,
    const QString& bodyDocumentText,
    const QString& bodyPlainText) const
{
    const ThinkingSpaceNoteVersionSnapshot* parentSnapshot = findSnapshot(state, parentSnapshotId);
    const QString previousHeader = parentSnapshot == nullptr ? QString() : parentSnapshot->headerText;
    const QString previousBody = parentSnapshot == nullptr ? QString() : parentSnapshot->bodyDocumentText;

    const ThinkingSpaceNoteVersionDiffBuilder diffBuilder;

    ThinkingSpaceNoteVersionSnapshot snapshot;
    snapshot.snapshotId = uniqueSnapshotId(*this, state, label, headerText, bodyDocumentText);
    snapshot.parentSnapshotId = parentSnapshotId;
    snapshot.sourceSnapshotId = sourceSnapshotId;
    snapshot.label = label;
    snapshot.createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    snapshot.commitModifiedCount = commitModifiedCount;
    snapshot.headerText = headerText;
    snapshot.bodyDocumentText = bodyDocumentText;
    snapshot.bodyPlainText = bodyPlainText;
    snapshot.headerDiff = diffBuilder.diffSegment(previousHeader, headerText, QStringLiteral("header.tsnhead"));
    snapshot.bodyDiff = diffBuilder.diffSegment(previousBody, bodyDocumentText, QStringLiteral("body.tsnbody"));
    return snapshot;
}
