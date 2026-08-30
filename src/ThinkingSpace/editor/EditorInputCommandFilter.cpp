#include "ThinkingSpace/editor/EditorInputCommandFilter.hpp"

#include "ThinkingSpace/clipboard/ClipboardEditorPaste.h"
#include "ThinkingSpace/editor/NoteEditorDocumentSession.hpp"

#include <QEvent>
#include <QKeyEvent>
#include <QMetaObject>

EditorInputCommandFilter::EditorInputCommandFilter(QObject* parent)
    : QObject(parent)
{
}

EditorInputCommandFilter::~EditorInputCommandFilter()
{
    clearEditorInputOwner();
}

bool EditorInputCommandFilter::attachEditorInputOwner(
    QObject* editorItem,
    QObject* editorOwner,
    QObject* clipboardEditorPasteObject,
    QObject* clipboardObject,
    QObject* noteEditorSessionObject)
{
    if (editorItem == nullptr
        || editorOwner == nullptr
        || clipboardEditorPasteObject == nullptr
        || clipboardObject == nullptr
        || noteEditorSessionObject == nullptr)
    {
        return false;
    }

    if (m_editorItem != editorItem)
    {
        clearEditorInputOwner();
        m_editorItem = editorItem;
        m_editorItem->installEventFilter(this);
        m_editorItemDestroyedConnection = connect(
            m_editorItem,
            &QObject::destroyed,
            this,
            [this]() {
                clearDestroyedEditorInputOwner();
            });
    }

    m_editorOwner = editorOwner;
    m_clipboardEditorPasteObject = clipboardEditorPasteObject;
    m_clipboardObject = clipboardObject;
    m_noteEditorSessionObject = noteEditorSessionObject;
    return true;
}

void EditorInputCommandFilter::detachEditorInputOwner(QObject* editorItem)
{
    if (editorItem != nullptr && m_editorItem != editorItem)
    {
        return;
    }
    clearEditorInputOwner();
}

bool EditorInputCommandFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editorItem && event != nullptr && event->type() == QEvent::KeyPress)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (handleEditorCalloutBoundaryKeyEvent(*keyEvent))
        {
            event->accept();
            return true;
        }
        if (handleEditorStyleBoundaryKeyEvent(*keyEvent))
        {
            event->accept();
            return true;
        }
        if (handleEditorPasteKeyEvent(*keyEvent))
        {
            event->accept();
            return true;
        }
    }

    return QObject::eventFilter(watched, event);
}

void EditorInputCommandFilter::clearEditorInputOwner()
{
    if (m_editorItem)
    {
        m_editorItem->removeEventFilter(this);
    }
    if (m_editorItemDestroyedConnection)
    {
        disconnect(m_editorItemDestroyedConnection);
        m_editorItemDestroyedConnection = QMetaObject::Connection();
    }
    m_editorItem.clear();
    m_editorOwner.clear();
    m_clipboardEditorPasteObject.clear();
    m_clipboardObject.clear();
    m_noteEditorSessionObject.clear();
}

void EditorInputCommandFilter::clearDestroyedEditorInputOwner()
{
    if (m_editorItemDestroyedConnection)
    {
        disconnect(m_editorItemDestroyedConnection);
        m_editorItemDestroyedConnection = QMetaObject::Connection();
    }
    m_editorItem.clear();
    m_editorOwner.clear();
    m_clipboardEditorPasteObject.clear();
    m_clipboardObject.clear();
    m_noteEditorSessionObject.clear();
}

bool EditorInputCommandFilter::editorPasteKeyMatches(const QKeyEvent& event) const
{
    if (event.isAutoRepeat() || event.key() != Qt::Key_V)
    {
        return false;
    }

    const Qt::KeyboardModifiers modifiers = event.modifiers();
    const bool commandModifier =
        modifiers.testFlag(Qt::MetaModifier) || modifiers.testFlag(Qt::ControlModifier);
    const bool disallowedModifiers =
        modifiers.testFlag(Qt::ShiftModifier) || modifiers.testFlag(Qt::AltModifier);
    return commandModifier && !disallowedModifiers;
}

bool EditorInputCommandFilter::editorCalloutBoundaryKeyMatches(const QKeyEvent& event) const
{
    if (event.isAutoRepeat())
    {
        return false;
    }

    const bool supportedKey =
        event.key() == Qt::Key_Backspace;
    if (!supportedKey)
    {
        return false;
    }

    const Qt::KeyboardModifiers modifiers = event.modifiers();
    return !modifiers.testFlag(Qt::ShiftModifier)
        && !modifiers.testFlag(Qt::ControlModifier)
        && !modifiers.testFlag(Qt::MetaModifier)
        && !modifiers.testFlag(Qt::AltModifier);
}

bool EditorInputCommandFilter::editorStyleBoundaryKeyMatches(const QKeyEvent& event) const
{
    if (event.isAutoRepeat())
    {
        return false;
    }

    const bool supportedKey =
        event.key() == Qt::Key_Return
        || event.key() == Qt::Key_Enter;
    if (!supportedKey)
    {
        return false;
    }

    const Qt::KeyboardModifiers modifiers = event.modifiers();
    return !modifiers.testFlag(Qt::ShiftModifier)
        && !modifiers.testFlag(Qt::ControlModifier)
        && !modifiers.testFlag(Qt::MetaModifier)
        && !modifiers.testFlag(Qt::AltModifier);
}

int EditorInputCommandFilter::editorCommandCursorPosition(
    const int selectionStart,
    const int selectionLength) const
{
    if (selectionLength > 0 || m_editorOwner.isNull())
    {
        return selectionStart;
    }

    const QVariant cursorPosition = m_editorOwner->property("cursorPosition");
    return cursorPosition.isValid() ? cursorPosition.toInt() : selectionStart;
}

bool EditorInputCommandFilter::handleEditorCalloutBoundaryKeyEvent(QKeyEvent& event)
{
    if (!editorCalloutBoundaryKeyMatches(event)
        || m_editorOwner.isNull()
        || m_noteEditorSessionObject.isNull())
    {
        return false;
    }

    auto* noteEditorSession = qobject_cast<NoteEditorDocumentSession*>(m_noteEditorSessionObject.data());
    if (noteEditorSession == nullptr)
    {
        return false;
    }

    const QString editorDocumentText =
        m_editorOwner->property("editorDocumentText").toString();
    const int selectionStart =
        m_editorOwner->property("editorSelectionStart").toInt();
    const int selectionLength =
        m_editorOwner->property("editorSelectionLength").toInt();
    const int cursorPosition = editorCommandCursorPosition(selectionStart, selectionLength);

    const QVariantMap result = noteEditorSession->handleCalloutBoundaryKeyInSource(
        editorDocumentText,
        cursorPosition,
        selectionLength,
        event.key());
    if (!result.value(QStringLiteral("handled")).toBool())
    {
        return false;
    }
    if (!result.value(QStringLiteral("valid")).toBool())
    {
        return true;
    }

    applyEditorCommandResultToOwner(result);
    event.accept();
    return true;
}

bool EditorInputCommandFilter::handleEditorStyleBoundaryKeyEvent(QKeyEvent& event)
{
    if (!editorStyleBoundaryKeyMatches(event)
        || m_editorOwner.isNull()
        || m_noteEditorSessionObject.isNull())
    {
        return false;
    }

    auto* noteEditorSession = qobject_cast<NoteEditorDocumentSession*>(m_noteEditorSessionObject.data());
    if (noteEditorSession == nullptr)
    {
        return false;
    }

    const QString editorDocumentText =
        m_editorOwner->property("editorDocumentText").toString();
    const int selectionStart =
        m_editorOwner->property("editorSelectionStart").toInt();
    const int selectionLength =
        m_editorOwner->property("editorSelectionLength").toInt();
    const int cursorPosition = editorCommandCursorPosition(selectionStart, selectionLength);

    const QVariantMap result = noteEditorSession->handleStyleBoundaryKeyInSource(
        editorDocumentText,
        cursorPosition,
        selectionLength,
        event.key());
    if (!result.value(QStringLiteral("handled")).toBool())
    {
        return false;
    }
    if (!result.value(QStringLiteral("valid")).toBool())
    {
        return true;
    }

    applyEditorCommandResultToOwner(result);
    event.accept();
    return true;
}

bool EditorInputCommandFilter::handleEditorPasteKeyEvent(QKeyEvent& event)
{
    if (!editorPasteKeyMatches(event)
        || m_editorOwner.isNull()
        || m_clipboardEditorPasteObject.isNull()
        || m_clipboardObject.isNull()
        || m_noteEditorSessionObject.isNull())
    {
        return false;
    }

    auto* editorPaste = qobject_cast<ClipboardEditorPaste*>(m_clipboardEditorPasteObject.data());
    if (editorPaste == nullptr)
    {
        return false;
    }

    const QString editorDocumentText =
        m_editorOwner->property("editorDocumentText").toString();
    const int selectionStart =
        m_editorOwner->property("editorSelectionStart").toInt();
    const int selectionLength =
        m_editorOwner->property("editorSelectionLength").toInt();

    const QVariantMap insertion = editorPaste->pasteImageResourceIntoEditor(
        m_clipboardObject.data(),
        m_noteEditorSessionObject.data(),
        editorDocumentText,
        selectionStart,
        selectionLength);
    if (insertion.value(QStringLiteral("nativePaste")).toBool())
    {
        return false;
    }
    if (!insertion.value(QStringLiteral("valid")).toBool())
    {
        return true;
    }

    if (!applyEditorCommandResultToOwner(insertion))
    {
        return true;
    }

    event.accept();
    return true;
}

bool EditorInputCommandFilter::applyEditorCommandResultToOwner(const QVariantMap& result)
{
    if (m_editorOwner.isNull())
    {
        return false;
    }

    const QString editorDocumentText =
        result.value(QStringLiteral("editorDocumentText")).isValid()
            ? result.value(QStringLiteral("editorDocumentText")).toString()
            : result.value(QStringLiteral("bodySourceText")).toString();
    QVariant replaceResult;
    const bool invoked = QMetaObject::invokeMethod(
        m_editorOwner.data(),
        "replaceEditorDocumentText",
        Q_RETURN_ARG(QVariant, replaceResult),
        Q_ARG(QVariant, QVariant(editorDocumentText)),
        Q_ARG(QVariant, QVariant(result.value(QStringLiteral("cursorPosition")).toInt())));
    return invoked && replaceResult.toBool();
}
