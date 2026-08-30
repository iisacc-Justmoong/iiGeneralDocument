#include "ThinkingSpace/clipboard/ClipboardEditorPaste.h"

#include "ThinkingSpace/clipboard/InAppClipboardManager.h"
#include "ThinkingSpace/editor/NoteEditorDocumentSession.hpp"

#include <QVariantList>

namespace
{
    QVariantMap editorPasteResult(
        const bool valid,
        const bool nativePaste,
        const QString& stage,
        const QString& errorMessage = QString())
    {
        QVariantMap result;
        result.insert(QStringLiteral("valid"), valid);
        result.insert(QStringLiteral("changed"), false);
        result.insert(QStringLiteral("nativePaste"), nativePaste);
        result.insert(QStringLiteral("reloadSucceeded"), true);
        result.insert(QStringLiteral("stage"), stage.trimmed());
        result.insert(QStringLiteral("errorMessage"), errorMessage.trimmed());
        return result;
    }

    bool clipboardResourceIsImage(const InAppClipboardManager& clipboard)
    {
        return clipboard.resourceType().trimmed().compare(QStringLiteral("image"), Qt::CaseInsensitive) == 0;
    }

    QVariantMap nativePasteFallbackResult(const QString& stage, const QString& errorMessage)
    {
        return editorPasteResult(false, true, stage, errorMessage);
    }

    QVariantMap handledFailureResult(const QString& stage, const QString& errorMessage)
    {
        return editorPasteResult(false, false, stage, errorMessage);
    }
} // namespace

ClipboardEditorPaste::ClipboardEditorPaste(QObject* parent)
    : QObject(parent)
{
}

QVariantMap ClipboardEditorPaste::pasteImageResourceIntoEditor(
    QObject* clipboardObject,
    QObject* noteEditorSessionObject,
    const QString& editorDocumentText,
    const int cursorPosition,
    const int selectionLength)
{
    auto* clipboard = qobject_cast<InAppClipboardManager*>(clipboardObject);
    if (clipboard == nullptr)
    {
        const QString errorMessage = QStringLiteral("Clipboard editor paste requires InAppClipboardManager.");
        emit pasteFailed(errorMessage);
        return nativePasteFallbackResult(QStringLiteral("clipboard-manager"), errorMessage);
    }

    auto* noteEditorSession = qobject_cast<NoteEditorDocumentSession*>(noteEditorSessionObject);
    if (noteEditorSession == nullptr)
    {
        const QString errorMessage = QStringLiteral("Clipboard editor paste requires NoteEditorDocumentSession.");
        emit pasteFailed(errorMessage);
        return handledFailureResult(QStringLiteral("note-session"), errorMessage);
    }
    if (!clipboard->refreshClipboardResourceAvailabilitySnapshot())
    {
        const QString errorMessage = QStringLiteral("Clipboard does not contain an importable resource.");
        return nativePasteFallbackResult(QStringLiteral("capture"), errorMessage);
    }

    if (!clipboardResourceIsImage(*clipboard))
    {
        const QString errorMessage =
            QStringLiteral("Only image clipboard resources can be pasted into the editor for now.");
        return nativePasteFallbackResult(QStringLiteral("unsupported-resource"), errorMessage);
    }

    const QVariantList importedEntries = clipboard->importClipboardResourceForEditor();
    if (importedEntries.isEmpty())
    {
        const QString errorMessage = clipboard->lastError().trimmed().isEmpty()
            ? QStringLiteral("Failed to import the clipboard image resource.")
            : clipboard->lastError().trimmed();
        emit pasteFailed(errorMessage);
        return handledFailureResult(QStringLiteral("import"), errorMessage);
    }

    QVariantMap insertion = noteEditorSession->insertImportedResourcesIntoSource(
        editorDocumentText,
        cursorPosition,
        selectionLength,
        importedEntries);
    if (!insertion.value(QStringLiteral("valid")).toBool())
    {
        const QString sessionError = noteEditorSession->lastError().trimmed();
        const QString errorMessage = sessionError.isEmpty()
            ? QStringLiteral("Failed to insert the imported resource into the editor source.")
            : sessionError;
        emit pasteFailed(errorMessage);
        return handledFailureResult(QStringLiteral("source-insertion"), errorMessage);
    }

    insertion.insert(QStringLiteral("nativePaste"), false);
    insertion.insert(QStringLiteral("importedEntries"), importedEntries);

    const bool reloadSucceeded = clipboard->reloadImportedResources();
    insertion.insert(QStringLiteral("reloadSucceeded"), reloadSucceeded);
    insertion.insert(
        QStringLiteral("stage"),
        reloadSucceeded ? QStringLiteral("completed") : QStringLiteral("reload"));
    if (!reloadSucceeded)
    {
        insertion.insert(QStringLiteral("errorMessage"), clipboard->lastError().trimmed());
    }
    else if (!insertion.contains(QStringLiteral("errorMessage")))
    {
        insertion.insert(QStringLiteral("errorMessage"), QString());
    }

    emit pasteCompleted(insertion);
    return insertion;
}
