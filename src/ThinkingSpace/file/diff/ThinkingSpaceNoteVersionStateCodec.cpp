#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionStateCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace
{
    constexpr auto kVersionSchema = "thinkingspace.note.version.store";

    QJsonObject emptyVersionRoot(const QString& noteId)
    {
        QJsonObject root;
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("schema"), QString::fromLatin1(kVersionSchema));
        root.insert(QStringLiteral("noteId"), noteId.trimmed());
        root.insert(QStringLiteral("currentSnapshotId"), QString());
        root.insert(QStringLiteral("headSnapshotId"), QString());
        root.insert(QStringLiteral("snapshots"), QJsonArray{});
        return root;
    }

    QJsonObject diffSegmentToJson(const ThinkingSpaceNoteVersionDiffSegment& segment)
    {
        QJsonObject object;
        object.insert(QStringLiteral("prefixLength"), segment.prefixLength);
        object.insert(QStringLiteral("suffixLength"), segment.suffixLength);
        object.insert(QStringLiteral("removedText"), segment.removedText);
        object.insert(QStringLiteral("insertedText"), segment.insertedText);
        object.insert(QStringLiteral("unifiedPatch"), segment.unifiedPatch);
        object.insert(QStringLiteral("generatedAtUtc"), segment.generatedAtUtc);
        return object;
    }

    ThinkingSpaceNoteVersionDiffSegment diffSegmentFromJson(const QJsonObject& object)
    {
        ThinkingSpaceNoteVersionDiffSegment segment;
        segment.prefixLength = object.value(QStringLiteral("prefixLength")).toInt();
        segment.suffixLength = object.value(QStringLiteral("suffixLength")).toInt();
        segment.removedText = object.value(QStringLiteral("removedText")).toString();
        segment.insertedText = object.value(QStringLiteral("insertedText")).toString();
        segment.unifiedPatch = object.value(QStringLiteral("unifiedPatch")).toString();
        segment.generatedAtUtc = object.value(QStringLiteral("generatedAtUtc")).toString();
        return segment;
    }

    QJsonObject snapshotToJson(const ThinkingSpaceNoteVersionSnapshot& snapshot)
    {
        QJsonObject object;
        object.insert(QStringLiteral("snapshotId"), snapshot.snapshotId);
        object.insert(QStringLiteral("parentSnapshotId"), snapshot.parentSnapshotId);
        object.insert(QStringLiteral("sourceSnapshotId"), snapshot.sourceSnapshotId);
        object.insert(QStringLiteral("label"), snapshot.label);
        object.insert(QStringLiteral("createdAtUtc"), snapshot.createdAtUtc);
        object.insert(QStringLiteral("commitModifiedCount"), snapshot.commitModifiedCount);
        object.insert(QStringLiteral("headerText"), snapshot.headerText);
        object.insert(QStringLiteral("bodyDocumentText"), snapshot.bodyDocumentText);
        object.insert(QStringLiteral("bodyPlainText"), snapshot.bodyPlainText);
        object.insert(QStringLiteral("headerDiff"), diffSegmentToJson(snapshot.headerDiff));
        object.insert(QStringLiteral("bodyDiff"), diffSegmentToJson(snapshot.bodyDiff));
        return object;
    }

    ThinkingSpaceNoteVersionSnapshot snapshotFromJson(const QJsonObject& object)
    {
        ThinkingSpaceNoteVersionSnapshot snapshot;
        snapshot.snapshotId = object.value(QStringLiteral("snapshotId")).toString();
        snapshot.parentSnapshotId = object.value(QStringLiteral("parentSnapshotId")).toString();
        snapshot.sourceSnapshotId = object.value(QStringLiteral("sourceSnapshotId")).toString();
        snapshot.label = object.value(QStringLiteral("label")).toString();
        snapshot.createdAtUtc = object.value(QStringLiteral("createdAtUtc")).toString();
        snapshot.commitModifiedCount = object.value(QStringLiteral("commitModifiedCount")).toInt();
        snapshot.headerText = object.value(QStringLiteral("headerText")).toString();
        snapshot.bodyDocumentText = object.value(QStringLiteral("bodyDocumentText")).toString();
        snapshot.bodyPlainText = object.value(QStringLiteral("bodyPlainText")).toString();
        snapshot.headerDiff = diffSegmentFromJson(object.value(QStringLiteral("headerDiff")).toObject());
        snapshot.bodyDiff = diffSegmentFromJson(object.value(QStringLiteral("bodyDiff")).toObject());
        return snapshot;
    }

    QJsonObject stateToJson(const ThinkingSpaceNoteVersionState& state)
    {
        QJsonArray snapshots;
        for (const ThinkingSpaceNoteVersionSnapshot& snapshot : state.snapshots)
        {
            snapshots.push_back(snapshotToJson(snapshot));
        }

        QJsonObject root = emptyVersionRoot(state.noteId);
        root.insert(QStringLiteral("currentSnapshotId"), state.currentSnapshotId);
        root.insert(QStringLiteral("headSnapshotId"), state.headSnapshotId);
        root.insert(QStringLiteral("snapshots"), snapshots);
        return root;
    }
} // namespace

QString ThinkingSpaceNoteVersionStateCodec::emptyStateText(const QString& noteId) const
{
    return QString::fromUtf8(QJsonDocument(emptyVersionRoot(noteId)).toJson(QJsonDocument::Indented));
}

QString ThinkingSpaceNoteVersionStateCodec::serializeState(const ThinkingSpaceNoteVersionState& state) const
{
    return QString::fromUtf8(QJsonDocument(stateToJson(state)).toJson(QJsonDocument::Indented));
}

bool ThinkingSpaceNoteVersionStateCodec::parseState(
    const QString& versionText,
    const QString& versionFilePath,
    ThinkingSpaceNoteVersionState* outState,
    QString* errorMessage) const
{
    if (outState == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("outState must not be null.");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(versionText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Failed to parse .tsnversion JSON: %1").arg(versionFilePath);
        }
        return false;
    }

    const QJsonObject root = document.object();
    ThinkingSpaceNoteVersionState state;
    state.noteId = root.value(QStringLiteral("noteId")).toString();
    state.currentSnapshotId = root.value(QStringLiteral("currentSnapshotId")).toString();
    state.headSnapshotId = root.value(QStringLiteral("headSnapshotId")).toString();

    const QJsonArray snapshots = root.value(QStringLiteral("snapshots")).toArray();
    state.snapshots.reserve(snapshots.size());
    for (const QJsonValue& value : snapshots)
    {
        const ThinkingSpaceNoteVersionSnapshot snapshot = snapshotFromJson(value.toObject());
        if (!snapshot.snapshotId.trimmed().isEmpty())
        {
            state.snapshots.push_back(snapshot);
        }
    }

    *outState = std::move(state);
    return true;
}
