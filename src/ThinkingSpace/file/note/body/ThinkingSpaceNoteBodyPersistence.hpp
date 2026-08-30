#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

namespace ThinkingSpace::NoteBodyPersistence
{
    QString normalizeBodyPlainText(QString text);
    QString serializeBodyDocument(const QString& noteId, const QString& bodySourceText);
    QStringList extractedInlineTagValues(const QString& bodySourceText);
    QString plainTextFromBodyDocument(const QString& bodyDocumentText);
    QString sourceTextFromBodyDocument(const QString& bodyDocumentText);
    QString htmlProjectionFromBodyDocument(const QString& bodyDocumentText, int editorViewportWidth = 0);
    QString editorHtmlDocumentFromProjection(const QString& bodyHtml);
    QString editorHtmlFromBodySource(const QString& noteId, const QString& bodySourceText, int editorViewportWidth = 0);
    QString sourceTextFromEditorDocument(const QString& noteId, const QString& editorDocumentText);
    QString firstLineFromBodyDocument(const QString& bodyDocumentText);
    QString firstLineFromBodyPlainText(const QString& text);
    QString resolveBodyPath(const QString& noteDirectoryPath);
    QString resolveHeaderPath(const QString& noteHeaderPath, const QString& noteDirectoryPath);
    bool persistBodyPlainText(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const QString& noteHeaderPath,
        const QString& bodyPlainText,
        QString* outNormalizedBodyText = nullptr,
        QString* outNormalizedBodySourceText = nullptr,
        QString* outLastModifiedAt = nullptr,
        QString* errorMessage = nullptr);
} // namespace ThinkingSpace::NoteBodyPersistence

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
