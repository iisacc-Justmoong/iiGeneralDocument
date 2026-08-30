#include "ThinkingSpace/editor/TagInsertionWriter.hpp"

#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"

#include <QDir>
#include <QFile>

#include <utility>

namespace
{
    QString normalizePath(const QString& path)
    {
        const QString trimmed = path.trimmed();
        return trimmed.isEmpty() ? QString() : QDir::cleanPath(trimmed);
    }

    QString readUtf8File(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }
} // namespace

TagInsertionWriter::TagInsertionWriter(QObject* parent)
    : QObject(parent)
    , m_setTag(this)
{
    connect(&m_setTag, &SetTag::tagNameChanged, this, &TagInsertionWriter::tagNameChanged);
}

QString TagInsertionWriter::tagName() const
{
    return m_setTag.tagName();
}

QString TagInsertionWriter::lastError() const
{
    return m_lastError;
}

QStringList TagInsertionWriter::availableTagNames() const
{
    return m_setTag.availableTagNames();
}

QVariantMap TagInsertionWriter::staticTagDescriptor(const QString& tagName) const
{
    return m_setTag.staticTagDescriptor(tagName);
}

bool TagInsertionWriter::configureTagName(const QString& tagName)
{
    const bool configured = m_setTag.configureTagName(tagName);
    updateLastError(configured ? QString() : m_setTag.lastError());
    return configured;
}

QVariantMap TagInsertionWriter::insertIntoNote(
    const QString& noteId,
    const QString& noteDirectoryPath,
    const int cursorPosition,
    const int selectionLength)
{
    return insertNamedTagIntoNote(m_setTag.tagName(), noteId, noteDirectoryPath, cursorPosition, selectionLength);
}

QVariantMap TagInsertionWriter::insertNamedTagIntoNote(
    const QString& tagName,
    const QString& noteId,
    const QString& noteDirectoryPath,
    const int cursorPosition,
    const int selectionLength)
{
    const QString normalizedNoteId = noteId.trimmed();
    const QString normalizedNoteDirectoryPath = normalizePath(noteDirectoryPath);
    if (normalizedNoteId.isEmpty() || normalizedNoteDirectoryPath.isEmpty())
    {
        return finishResult(buildFailureResult(
            tagName,
            normalizedNoteId,
            normalizedNoteDirectoryPath,
            cursorPosition,
            selectionLength,
            QString(),
            QStringLiteral("noteId and noteDirectoryPath must not be empty.")));
    }

    ThinkingSpaceLocalNoteDocument document;
    ThinkingSpaceLocalNoteFileStore::ReadRequest readRequest;
    readRequest.noteId = normalizedNoteId;
    readRequest.noteDirectoryPath = normalizedNoteDirectoryPath;

    QString readError;
    if (!m_fileStore.readNote(readRequest, &document, &readError))
    {
        return finishResult(buildFailureResult(
            tagName,
            normalizedNoteId,
            normalizedNoteDirectoryPath,
            cursorPosition,
            selectionLength,
            QString(),
            readError.trimmed().isEmpty() ? QStringLiteral("Failed to read note before tag insertion.") : readError));
    }

    const QString bodySourceText = document.effectiveBodyText();
    QVariantMap insertionResult = m_setTag.insertNamedTagIntoSource(
        tagName,
        bodySourceText,
        cursorPosition,
        selectionLength);
    if (!insertionResult.value(QStringLiteral("valid")).toBool())
    {
        insertionResult.insert(QStringLiteral("noteId"), normalizedNoteId);
        insertionResult.insert(QStringLiteral("noteDirectoryPath"), normalizedNoteDirectoryPath);
        insertionResult.insert(QStringLiteral("noteBodyPath"), document.noteBodyPath);
        return finishResult(insertionResult);
    }

    emit tagWriteRequested(
        normalizedNoteId,
        normalizedNoteDirectoryPath,
        insertionResult.value(QStringLiteral("tagName")).toString());

    const QString mutatedSourceText = insertionResult.value(QStringLiteral("bodySourceText")).toString();
    document.bodyPlainText = mutatedSourceText;
    document.bodySourceText = mutatedSourceText;

    ThinkingSpaceLocalNoteFileStore::UpdateRequest updateRequest;
    updateRequest.document = std::move(document);
    updateRequest.persistHeader = false;
    updateRequest.persistBody = true;
    updateRequest.touchLastModified = true;
    updateRequest.incrementModifiedCount = true;
    updateRequest.refreshIncomingBacklinkStatistics = false;
    updateRequest.refreshAffectedBacklinkTargets = false;

    ThinkingSpaceLocalNoteDocument persistedDocument;
    QString updateError;
    if (!m_fileStore.updateNote(std::move(updateRequest), &persistedDocument, &updateError))
    {
        return finishResult(buildFailureResult(
            tagName,
            normalizedNoteId,
            normalizedNoteDirectoryPath,
            cursorPosition,
            selectionLength,
            bodySourceText,
            updateError.trimmed().isEmpty() ? QStringLiteral("Failed to persist tag insertion.") : updateError));
    }

    const QString bodyPath = persistedDocument.noteBodyPath.trimmed().isEmpty()
        ? ThinkingSpace::NoteBodyPersistence::resolveBodyPath(persistedDocument.noteDirectoryPath)
        : persistedDocument.noteBodyPath;

    insertionResult.insert(QStringLiteral("noteId"), persistedDocument.headerStore.noteId().trimmed());
    insertionResult.insert(QStringLiteral("noteDirectoryPath"), persistedDocument.noteDirectoryPath.trimmed());
    insertionResult.insert(QStringLiteral("noteBodyPath"), bodyPath);
    insertionResult.insert(QStringLiteral("bodyDocumentText"), readUtf8File(bodyPath));
    return finishResult(insertionResult);
}

void TagInsertionWriter::setTagName(const QString& tagName)
{
    configureTagName(tagName);
}

void TagInsertionWriter::clearLastError()
{
    updateLastError(QString());
}

QVariantMap TagInsertionWriter::buildFailureResult(
    const QString& tagName,
    const QString& noteId,
    const QString& noteDirectoryPath,
    const int cursorPosition,
    const int selectionLength,
    const QString& bodySourceText,
    const QString& errorMessage)
{
    QVariantMap result;
    result.insert(QStringLiteral("valid"), false);
    result.insert(QStringLiteral("changed"), false);
    result.insert(QStringLiteral("tagName"), tagName.trimmed());
    result.insert(QStringLiteral("noteId"), noteId.trimmed());
    result.insert(QStringLiteral("noteDirectoryPath"), normalizePath(noteDirectoryPath));
    result.insert(QStringLiteral("noteBodyPath"), QString());
    result.insert(
        QStringLiteral("bodySourceText"),
        ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodySourceText));
    result.insert(QStringLiteral("bodyDocumentText"), QString());
    result.insert(QStringLiteral("cursorPosition"), cursorPosition);
    result.insert(QStringLiteral("selectionStart"), cursorPosition);
    result.insert(QStringLiteral("selectionLength"), selectionLength);
    result.insert(QStringLiteral("selectedText"), QString());
    result.insert(QStringLiteral("insertedText"), QString());
    result.insert(QStringLiteral("openingToken"), QString());
    result.insert(QStringLiteral("closingToken"), QString());
    result.insert(QStringLiteral("errorMessage"), errorMessage);
    return result;
}

QVariantMap TagInsertionWriter::finishResult(QVariantMap result)
{
    updateLastError(result.value(QStringLiteral("valid")).toBool()
                       ? QString()
                       : result.value(QStringLiteral("errorMessage")).toString());
    emit tagWriteFinished(result);
    return result;
}

void TagInsertionWriter::updateLastError(const QString& message)
{
    if (m_lastError == message)
    {
        return;
    }

    m_lastError = message;
    emit lastErrorChanged();
}
