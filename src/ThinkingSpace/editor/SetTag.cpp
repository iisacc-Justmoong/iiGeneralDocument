#include "ThinkingSpace/editor/SetTag.h"

#include "ThinkingSpace/editor/component/style.h"
#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"

#include <algorithm>

namespace
{
    enum class StaticBodyTagInsertionMode
    {
        WrapSelection,
        StandaloneLine
    };

    struct StaticBodyTag
    {
        QString canonicalName;
        QString openingToken;
        QString closingToken;
        StaticBodyTagInsertionMode insertionMode = StaticBodyTagInsertionMode::WrapSelection;

        bool isValid() const noexcept
        {
            return !canonicalName.isEmpty() && !openingToken.isEmpty();
        }
    };

    QString normalizedTagName(const QString& tagName)
    {
        QString normalized;
        const QString trimmed = tagName.trimmed();
        normalized.reserve(trimmed.size());
        for (const QChar ch : trimmed)
        {
            if (ch.isLetterOrNumber())
            {
                normalized.push_back(ch.toCaseFolded());
            }
        }
        return normalized;
    }

    StaticBodyTag staticBodyTagFor(const QString& tagName)
    {
        const QString normalized = normalizedTagName(tagName);
        if (normalized == QStringLiteral("callout"))
        {
            return {
                QStringLiteral("callout"),
                QStringLiteral("<callout>"),
                QStringLiteral("</callout>")
            };
        }
        if (normalized == QStringLiteral("event"))
        {
            return {
                QStringLiteral("event"),
                QStringLiteral("<event>"),
                QStringLiteral("</event>")
            };
        }
        if (normalized == QStringLiteral("header"))
        {
            return {
                QStringLiteral("header"),
                QStringLiteral("<header>"),
                QStringLiteral("</header>")
            };
        }
        if (normalized == QStringLiteral("subheader"))
        {
            return {
                QStringLiteral("subheader"),
                QStringLiteral("<subheader>"),
                QStringLiteral("</subheader>")
            };
        }
        if (normalized == QStringLiteral("title") || normalized == QStringLiteral("h1"))
        {
            return {
                QStringLiteral("title"),
                QStringLiteral("<title>"),
                QStringLiteral("</title>")
            };
        }
        if (normalized == QStringLiteral("subtitle") || normalized == QStringLiteral("h2"))
        {
            return {
                QStringLiteral("subtitle"),
                QStringLiteral("<subtitle>"),
                QStringLiteral("</subtitle>")
            };
        }
        if (normalized == QStringLiteral("eventtitle") || normalized == QStringLiteral("h3"))
        {
            return {
                QStringLiteral("eventTitle"),
                QStringLiteral("<eventTitle>"),
                QStringLiteral("</eventTitle>")
            };
        }
        if (normalized == QStringLiteral("eventdescription"))
        {
            return {
                QStringLiteral("eventDescription"),
                QStringLiteral("<eventDescription>"),
                QStringLiteral("</eventDescription>")
            };
        }
        if (normalized == QStringLiteral("bold")
            || normalized == QStringLiteral("b")
            || normalized == QStringLiteral("strong"))
        {
            return {
                QStringLiteral("bold"),
                ThinkingSpace::EditorComponent::Style::openingTokenForFontWeight(QStringLiteral("900")),
                ThinkingSpace::EditorComponent::Style::closingToken()
            };
        }
        if (normalized == QStringLiteral("italic")
            || normalized == QStringLiteral("i")
            || normalized == QStringLiteral("em"))
        {
            return {
                QStringLiteral("italic"),
                QStringLiteral("<italic>"),
                QStringLiteral("</italic>")
            };
        }
        if (normalized == QStringLiteral("underline") || normalized == QStringLiteral("u"))
        {
            return {
                QStringLiteral("underline"),
                QStringLiteral("<underline>"),
                QStringLiteral("</underline>")
            };
        }
        if (normalized == QStringLiteral("strikethrough")
            || normalized == QStringLiteral("strike")
            || normalized == QStringLiteral("s")
            || normalized == QStringLiteral("del"))
        {
            return {
                QStringLiteral("strikethrough"),
                QStringLiteral("<strikethrough>"),
                QStringLiteral("</strikethrough>")
            };
        }
        if (normalized == QStringLiteral("highlight") || normalized == QStringLiteral("mark"))
        {
            return {
                QStringLiteral("highlight"),
                QStringLiteral("<highlight>"),
                QStringLiteral("</highlight>")
            };
        }
        if (normalized == QStringLiteral("tag"))
        {
            return {
                QStringLiteral("tag"),
                QStringLiteral("<tag>"),
                QStringLiteral("</tag>")
            };
        }
        if (normalized == QStringLiteral("style"))
        {
            return {
                ThinkingSpace::EditorComponent::Style::canonicalName(),
                ThinkingSpace::EditorComponent::Style::openingToken(),
                ThinkingSpace::EditorComponent::Style::closingToken()
            };
        }
        if (normalized == QStringLiteral("break")
            || normalized == QStringLiteral("hr")
            || normalized == QStringLiteral("divider"))
        {
            return {
                QStringLiteral("break"),
                QStringLiteral("</break>"),
                QString(),
                StaticBodyTagInsertionMode::StandaloneLine
            };
        }
        if (normalized == QStringLiteral("resource"))
        {
            return {
                QStringLiteral("resource"),
                QStringLiteral("<resource />"),
                QString(),
                StaticBodyTagInsertionMode::StandaloneLine
            };
        }
        return {};
    }

    QStringList staticBodyTagNames()
    {
        QStringList tagNames{
            QStringLiteral("callout"),
        };
        tagNames.append({
            QStringLiteral("event"),
            QStringLiteral("header"),
            QStringLiteral("subheader"),
            QStringLiteral("title"),
            QStringLiteral("subtitle"),
            QStringLiteral("eventTitle"),
            QStringLiteral("eventDescription"),
            QStringLiteral("bold"),
            QStringLiteral("italic"),
            QStringLiteral("underline"),
            QStringLiteral("strikethrough"),
            QStringLiteral("highlight"),
            QStringLiteral("tag"),
            QStringLiteral("style"),
            QStringLiteral("break"),
            QStringLiteral("resource")
        });
        return tagNames;
    }

    int clampedPosition(const int position, const int textSize)
    {
        return std::clamp(position, 0, textSize);
    }

    QVariantMap buildInvalidResult(
        const QString& requestedTagName,
        const QString& bodySourceText,
        const int cursorPosition,
        const int selectionLength,
        const QString& errorMessage)
    {
        const QString normalizedSourceText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodySourceText);
        const int sourceSize = normalizedSourceText.size();
        const int anchor = clampedPosition(cursorPosition, sourceSize);
        const int active = clampedPosition(cursorPosition + selectionLength, sourceSize);
        const int selectionStart = std::min(anchor, active);
        const int selectionEnd = std::max(anchor, active);

        QVariantMap result;
        result.insert(QStringLiteral("valid"), false);
        result.insert(QStringLiteral("changed"), false);
        result.insert(QStringLiteral("tagName"), requestedTagName.trimmed());
        result.insert(QStringLiteral("bodySourceText"), normalizedSourceText);
        result.insert(QStringLiteral("cursorPosition"), anchor);
        result.insert(QStringLiteral("selectionStart"), selectionStart);
        result.insert(QStringLiteral("selectionLength"), selectionEnd - selectionStart);
        result.insert(
            QStringLiteral("selectedText"),
            normalizedSourceText.mid(selectionStart, selectionEnd - selectionStart));
        result.insert(QStringLiteral("insertedText"), QString());
        result.insert(QStringLiteral("errorMessage"), errorMessage);
        return result;
    }

    QVariantMap buildSourceInsertionResult(
        const StaticBodyTag& bodyTag,
        const QString& bodySourceText,
        const int cursorPosition,
        const int selectionLength)
    {
        const QString normalizedSourceText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodySourceText);
        const int sourceSize = normalizedSourceText.size();
        const int anchor = clampedPosition(cursorPosition, sourceSize);
        const int active = clampedPosition(cursorPosition + selectionLength, sourceSize);
        const int selectionStart = std::min(anchor, active);
        const int selectionEnd = std::max(anchor, active);
        const int normalizedSelectionLength = selectionEnd - selectionStart;
        const QString selectedText = normalizedSourceText.mid(selectionStart, normalizedSelectionLength);
        bool toggledOff = false;

        QString insertedText;
        QString mutatedSourceText;
        int nextCursorPosition = selectionStart;
        if (bodyTag.insertionMode == StaticBodyTagInsertionMode::StandaloneLine)
        {
            const QString beforeSelection = normalizedSourceText.left(selectionStart);
            const QString afterSelection = normalizedSourceText.mid(selectionEnd);
            const QString prefix = !beforeSelection.isEmpty() && !beforeSelection.endsWith(QLatin1Char('\n'))
                ? QStringLiteral("\n")
                : QString();
            const QString suffix = !afterSelection.isEmpty() && !afterSelection.startsWith(QLatin1Char('\n'))
                ? QStringLiteral("\n")
                : QString();
            insertedText = prefix + bodyTag.openingToken + suffix;
            mutatedSourceText = beforeSelection + insertedText + afterSelection;
            nextCursorPosition = beforeSelection.size() + prefix.size() + bodyTag.openingToken.size();
        }
        else
        {
            const QString beforeSelection = normalizedSourceText.left(selectionStart);
            const QString afterSelection = normalizedSourceText.mid(selectionEnd);
            toggledOff =
                normalizedSelectionLength > 0
                && !bodyTag.closingToken.isEmpty()
                && beforeSelection.endsWith(bodyTag.openingToken)
                && afterSelection.startsWith(bodyTag.closingToken);
            if (toggledOff)
            {
                insertedText = selectedText;
                const QString beforeOpeningToken =
                    beforeSelection.left(beforeSelection.size() - bodyTag.openingToken.size());
                mutatedSourceText =
                    beforeOpeningToken
                    + selectedText
                    + afterSelection.mid(bodyTag.closingToken.size());
                nextCursorPosition = beforeOpeningToken.size() + selectedText.size();
            }
            else
            {
                insertedText = bodyTag.openingToken + selectedText + bodyTag.closingToken;
                mutatedSourceText =
                    beforeSelection
                    + insertedText
                    + afterSelection;
                nextCursorPosition = selectedText.isEmpty()
                    ? selectionStart + bodyTag.openingToken.size()
                    : selectionStart + insertedText.size();
            }
        }

        QVariantMap result;
        result.insert(QStringLiteral("valid"), true);
        result.insert(QStringLiteral("changed"), true);
        result.insert(QStringLiteral("tagName"), bodyTag.canonicalName);
        result.insert(QStringLiteral("bodySourceText"), mutatedSourceText);
        result.insert(QStringLiteral("cursorPosition"), nextCursorPosition);
        result.insert(QStringLiteral("selectionStart"), selectionStart);
        result.insert(QStringLiteral("selectionLength"), normalizedSelectionLength);
        result.insert(QStringLiteral("selectedText"), selectedText);
        result.insert(QStringLiteral("insertedText"), insertedText);
        result.insert(QStringLiteral("openingToken"), bodyTag.openingToken);
        result.insert(QStringLiteral("closingToken"), bodyTag.closingToken);
        result.insert(QStringLiteral("toggledOff"), toggledOff);
        result.insert(QStringLiteral("errorMessage"), QString());
        return result;
    }
} // namespace

SetTag::SetTag(QObject* parent)
    : QObject(parent)
    , m_tagName(QStringLiteral("callout"))
{
}

QString SetTag::tagName() const
{
    return m_tagName;
}

QString SetTag::lastError() const
{
    return m_lastError;
}

QStringList SetTag::availableTagNames() const
{
    return staticBodyTagNames();
}

QVariantMap SetTag::staticTagDescriptor(const QString& tagName) const
{
    const StaticBodyTag bodyTag = staticBodyTagFor(tagName);
    QVariantMap descriptor;
    descriptor.insert(QStringLiteral("valid"), bodyTag.isValid());
    descriptor.insert(QStringLiteral("tagName"), bodyTag.canonicalName);
    descriptor.insert(QStringLiteral("openingToken"), bodyTag.openingToken);
    descriptor.insert(QStringLiteral("closingToken"), bodyTag.closingToken);
    descriptor.insert(QStringLiteral("emptyInsertionText"), bodyTag.openingToken + bodyTag.closingToken);
    descriptor.insert(QStringLiteral("emptyCursorOffset"), bodyTag.openingToken.size());
    descriptor.insert(
        QStringLiteral("standaloneLine"),
        bodyTag.insertionMode == StaticBodyTagInsertionMode::StandaloneLine);
    return descriptor;
}

bool SetTag::configureTagName(const QString& tagName)
{
    const StaticBodyTag bodyTag = staticBodyTagFor(tagName);
    if (!bodyTag.isValid())
    {
        updateLastError(QStringLiteral("Unsupported static body tag: %1").arg(tagName.trimmed()));
        return false;
    }

    clearLastError();
    if (m_tagName == bodyTag.canonicalName)
    {
        return true;
    }

    m_tagName = bodyTag.canonicalName;
    emit tagNameChanged();
    return true;
}

QVariantMap SetTag::insertIntoSource(
    const QString& bodySourceText,
    const int cursorPosition,
    const int selectionLength)
{
    return insertNamedTagIntoSource(m_tagName, bodySourceText, cursorPosition, selectionLength);
}

QVariantMap SetTag::insertNamedTagIntoSource(
    const QString& tagName,
    const QString& bodySourceText,
    const int cursorPosition,
    const int selectionLength)
{
    const StaticBodyTag bodyTag = staticBodyTagFor(tagName);
    if (!bodyTag.isValid())
    {
        const QString errorMessage = QStringLiteral("Unsupported static body tag: %1").arg(tagName.trimmed());
        updateLastError(errorMessage);
        return buildInvalidResult(tagName, bodySourceText, cursorPosition, selectionLength, errorMessage);
    }

    clearLastError();
    QVariantMap result = buildSourceInsertionResult(bodyTag, bodySourceText, cursorPosition, selectionLength);
    emit tagInserted(result);
    return result;
}

QVariantMap SetTag::insertStyleTagIntoSource(
    const QString& styleValue,
    const QString& bodySourceText,
    const int cursorPosition,
    const int selectionLength)
{
    const QString openingToken =
        ThinkingSpace::EditorComponent::Style::openingTokenForStyleAttributeValue(styleValue);
    if (openingToken.isEmpty())
    {
        const QString errorMessage =
            QStringLiteral("Unsupported style tag style value: %1").arg(styleValue.trimmed());
        updateLastError(errorMessage);
        return buildInvalidResult(
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            bodySourceText,
            cursorPosition,
            selectionLength,
            errorMessage);
    }

    clearLastError();
    QVariantMap result = buildSourceInsertionResult(
        {
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            openingToken,
            ThinkingSpace::EditorComponent::Style::closingToken()
        },
        bodySourceText,
        cursorPosition,
        selectionLength);
    result.insert(
        QStringLiteral("styleValue"),
        ThinkingSpace::EditorComponent::Style::normalizedStyleAttributeValue(styleValue));
    emit tagInserted(result);
    return result;
}

QVariantMap SetTag::insertStyleFontTagIntoSource(
    const QString& fontFamily,
    const QString& bodySourceText,
    const int cursorPosition,
    const int selectionLength)
{
    const QString openingToken =
        ThinkingSpace::EditorComponent::Style::openingTokenForFontFamily(fontFamily);
    if (openingToken.isEmpty())
    {
        const QString errorMessage =
            QStringLiteral("Unsupported style tag font value: %1").arg(fontFamily.trimmed());
        updateLastError(errorMessage);
        return buildInvalidResult(
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            bodySourceText,
            cursorPosition,
            selectionLength,
            errorMessage);
    }

    clearLastError();
    QVariantMap result = buildSourceInsertionResult(
        {
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            openingToken,
            ThinkingSpace::EditorComponent::Style::closingToken()
        },
        bodySourceText,
        cursorPosition,
        selectionLength);
    result.insert(
        QStringLiteral("fontFamily"),
        ThinkingSpace::EditorComponent::Style::normalizedFontFamilyAttributeValue(fontFamily));
    emit tagInserted(result);
    return result;
}

QVariantMap SetTag::insertStyleFontSizeTagIntoSource(
    const QString& fontSize,
    const QString& bodySourceText,
    const int cursorPosition,
    const int selectionLength)
{
    const QString openingToken =
        ThinkingSpace::EditorComponent::Style::openingTokenForFontSize(fontSize);
    if (openingToken.isEmpty())
    {
        const QString errorMessage =
            QStringLiteral("Unsupported style tag font size value: %1").arg(fontSize.trimmed());
        updateLastError(errorMessage);
        return buildInvalidResult(
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            bodySourceText,
            cursorPosition,
            selectionLength,
            errorMessage);
    }

    clearLastError();
    QVariantMap result = buildSourceInsertionResult(
        {
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            openingToken,
            ThinkingSpace::EditorComponent::Style::closingToken()
        },
        bodySourceText,
        cursorPosition,
        selectionLength);
    result.insert(
        QStringLiteral("fontSize"),
        ThinkingSpace::EditorComponent::Style::normalizedFontSizeAttributeValue(fontSize));
    emit tagInserted(result);
    return result;
}

QVariantMap SetTag::insertStyleFontWeightTagIntoSource(
    const QString& fontWeight,
    const QString& bodySourceText,
    const int cursorPosition,
    const int selectionLength)
{
    const QString openingToken =
        ThinkingSpace::EditorComponent::Style::openingTokenForFontWeight(fontWeight);
    if (openingToken.isEmpty())
    {
        const QString errorMessage =
            QStringLiteral("Unsupported style tag font weight value: %1").arg(fontWeight.trimmed());
        updateLastError(errorMessage);
        return buildInvalidResult(
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            bodySourceText,
            cursorPosition,
            selectionLength,
            errorMessage);
    }

    clearLastError();
    QVariantMap result = buildSourceInsertionResult(
        {
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            openingToken,
            ThinkingSpace::EditorComponent::Style::closingToken()
        },
        bodySourceText,
        cursorPosition,
        selectionLength);
    result.insert(
        QStringLiteral("fontWeight"),
        ThinkingSpace::EditorComponent::Style::normalizedFontWeightAttributeValue(fontWeight));
    emit tagInserted(result);
    return result;
}

QVariantMap SetTag::insertStyleBackgroundTagIntoSource(
    const QString& backgroundColor,
    const QString& bodySourceText,
    const int cursorPosition,
    const int selectionLength)
{
    const QString openingToken =
        ThinkingSpace::EditorComponent::Style::openingTokenForBackground(backgroundColor);
    if (openingToken.isEmpty())
    {
        const QString errorMessage =
            QStringLiteral("Unsupported style tag background value: %1").arg(backgroundColor.trimmed());
        updateLastError(errorMessage);
        return buildInvalidResult(
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            bodySourceText,
            cursorPosition,
            selectionLength,
            errorMessage);
    }

    clearLastError();
    QVariantMap result = buildSourceInsertionResult(
        {
            ThinkingSpace::EditorComponent::Style::canonicalName(),
            openingToken,
            ThinkingSpace::EditorComponent::Style::closingToken()
        },
        bodySourceText,
        cursorPosition,
        selectionLength);
    result.insert(
        QStringLiteral("backgroundColor"),
        ThinkingSpace::EditorComponent::Style::normalizedBackgroundAttributeValue(backgroundColor));
    emit tagInserted(result);
    return result;
}

QVariantMap SetTag::insertIntoBodyDocument(
    const QString& noteId,
    const QString& bodyDocumentText,
    const int cursorPosition,
    const int selectionLength)
{
    return insertNamedTagIntoBodyDocument(m_tagName, noteId, bodyDocumentText, cursorPosition, selectionLength);
}

QVariantMap SetTag::insertNamedTagIntoBodyDocument(
    const QString& tagName,
    const QString& noteId,
    const QString& bodyDocumentText,
    const int cursorPosition,
    const int selectionLength)
{
    const QString sourceText = bodyDocumentText.trimmed().isEmpty()
        ? QString()
        : ThinkingSpace::NoteBodyPersistence::sourceTextFromBodyDocument(bodyDocumentText);

    const StaticBodyTag bodyTag = staticBodyTagFor(tagName);
    if (!bodyTag.isValid())
    {
        const QString errorMessage = QStringLiteral("Unsupported static body tag: %1").arg(tagName.trimmed());
        updateLastError(errorMessage);
        QVariantMap result = buildInvalidResult(tagName, sourceText, cursorPosition, selectionLength, errorMessage);
        result.insert(QStringLiteral("bodyDocumentText"), bodyDocumentText);
        return result;
    }

    clearLastError();
    QVariantMap result = buildSourceInsertionResult(bodyTag, sourceText, cursorPosition, selectionLength);
    result.insert(
        QStringLiteral("bodyDocumentText"),
        ThinkingSpace::NoteBodyPersistence::serializeBodyDocument(
            noteId,
            result.value(QStringLiteral("bodySourceText")).toString()));
    emit tagInserted(result);
    return result;
}

void SetTag::setTagName(const QString& tagName)
{
    configureTagName(tagName);
}

void SetTag::clearLastError()
{
    updateLastError(QString());
}

void SetTag::updateLastError(const QString& message)
{
    if (m_lastError == message)
    {
        return;
    }

    m_lastError = message;
    emit lastErrorChanged();
}
