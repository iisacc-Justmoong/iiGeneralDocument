#include "ThinkingSpace/editor/GetProperty.h"

#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    struct TagRange
    {
        int start = -1;
        int end = -1;

        bool isValid() const noexcept
        {
            return start >= 0 && end >= start;
        }
    };

    struct ParsedAttribute
    {
        QString name;
        QVariant value;
        QString valueKind;
        QString literal;
    };

    bool isNameStartCharacter(const QChar ch)
    {
        return ch.isLetter() || ch == QLatin1Char('_');
    }

    bool isNameCharacter(const QChar ch)
    {
        return ch.isLetterOrNumber()
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('-')
            || ch == QLatin1Char('.')
            || ch == QLatin1Char(':');
    }

    QString normalizedPropertyName(const QString& propertyName)
    {
        return propertyName.trimmed();
    }

    QString unescapeXmlAttributeValue(QString value)
    {
        value.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        value.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
        value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        return value;
    }

    int clampedPosition(const int position, const int textSize)
    {
        return std::clamp(position, 0, std::max(0, textSize - 1));
    }

    TagRange tagRangeAtPosition(const QString& sourceText, const int tagPosition)
    {
        if (sourceText.isEmpty())
        {
            return {};
        }

        const int cursor = clampedPosition(tagPosition, sourceText.size());
        const int tagStart = sourceText.lastIndexOf(QLatin1Char('<'), cursor);
        if (tagStart < 0)
        {
            return {};
        }

        const int tagEnd = sourceText.indexOf(QLatin1Char('>'), tagStart + 1);
        if (tagEnd < 0 || cursor > tagEnd)
        {
            return {};
        }

        int nameCursor = tagStart + 1;
        while (nameCursor < tagEnd && sourceText.at(nameCursor).isSpace())
        {
            ++nameCursor;
        }
        if (nameCursor >= tagEnd)
        {
            return {};
        }

        const QChar marker = sourceText.at(nameCursor);
        if (marker == QLatin1Char('/')
            || marker == QLatin1Char('!')
            || marker == QLatin1Char('?'))
        {
            return {};
        }

        return { tagStart, tagEnd };
    }

    int tagNameStartOffset(const QString& tagText)
    {
        int cursor = 1;
        while (cursor < tagText.size() && tagText.at(cursor).isSpace())
        {
            ++cursor;
        }
        return cursor;
    }

    int tagNameEndOffset(const QString& tagText)
    {
        int cursor = tagNameStartOffset(tagText);
        while (cursor < tagText.size() && isNameCharacter(tagText.at(cursor)))
        {
            ++cursor;
        }
        return cursor;
    }

    QString tagNameFromTagText(const QString& tagText)
    {
        const int nameStart = tagNameStartOffset(tagText);
        const int nameEnd = tagNameEndOffset(tagText);
        return tagText.mid(nameStart, nameEnd - nameStart);
    }

    int skipQuotedValue(const QString& tagText, int cursor, const QChar quote)
    {
        ++cursor;
        while (cursor < tagText.size() && tagText.at(cursor) != quote)
        {
            ++cursor;
        }
        return cursor < tagText.size() ? cursor + 1 : cursor;
    }

    int skipUnquotedValue(const QString& tagText, int cursor)
    {
        while (cursor < tagText.size())
        {
            const QChar ch = tagText.at(cursor);
            if (ch.isSpace() || ch == QLatin1Char('/') || ch == QLatin1Char('>'))
            {
                break;
            }
            ++cursor;
        }
        return cursor;
    }

    bool readAttributeLiteral(
        const QString& tagText,
        int* cursor,
        QString* literal,
        bool* quoted)
    {
        while (*cursor < tagText.size() && tagText.at(*cursor).isSpace())
        {
            ++(*cursor);
        }
        if (*cursor >= tagText.size())
        {
            return false;
        }

        const QChar quote = tagText.at(*cursor);
        if (quote == QLatin1Char('"') || quote == QLatin1Char('\''))
        {
            const int valueStart = *cursor + 1;
            const int valueEnd = skipQuotedValue(tagText, *cursor, quote) - 1;
            *cursor = valueEnd < tagText.size() ? valueEnd + 1 : valueEnd;
            *literal = tagText.mid(valueStart, std::max(0, valueEnd - valueStart));
            *quoted = true;
            return true;
        }

        const int valueStart = *cursor;
        *cursor = skipUnquotedValue(tagText, *cursor);
        *literal = tagText.mid(valueStart, *cursor - valueStart);
        *quoted = false;
        return !literal->isEmpty();
    }

    QVariant parseAttributeValue(const QString& literal, const bool quoted, QString* valueKind)
    {
        if (quoted)
        {
            *valueKind = QStringLiteral("string");
            return unescapeXmlAttributeValue(literal);
        }

        const QString trimmed = literal.trimmed();
        if (trimmed == QStringLiteral("true") || trimmed == QStringLiteral("false"))
        {
            *valueKind = QStringLiteral("bool");
            return trimmed == QStringLiteral("true");
        }

        bool integerOk = false;
        const qlonglong integerValue = trimmed.toLongLong(&integerOk);
        if (integerOk)
        {
            *valueKind = QStringLiteral("int");
            return integerValue;
        }

        bool floatOk = false;
        const double floatValue = trimmed.toDouble(&floatOk);
        if (floatOk && std::isfinite(floatValue))
        {
            *valueKind = QStringLiteral("float");
            return floatValue;
        }

        *valueKind = QStringLiteral("string");
        return trimmed;
    }

    QList<ParsedAttribute> attributesFromTagText(const QString& tagText)
    {
        QList<ParsedAttribute> attributes;
        int cursor = tagNameEndOffset(tagText);
        while (cursor >= 0 && cursor < tagText.size())
        {
            while (cursor < tagText.size() && tagText.at(cursor).isSpace())
            {
                ++cursor;
            }
            if (cursor >= tagText.size()
                || tagText.at(cursor) == QLatin1Char('/')
                || tagText.at(cursor) == QLatin1Char('>'))
            {
                break;
            }

            if (!isNameStartCharacter(tagText.at(cursor)))
            {
                ++cursor;
                continue;
            }

            const int nameStart = cursor;
            ++cursor;
            while (cursor < tagText.size() && isNameCharacter(tagText.at(cursor)))
            {
                ++cursor;
            }
            const QString attributeName = tagText.mid(nameStart, cursor - nameStart);

            while (cursor < tagText.size() && tagText.at(cursor).isSpace())
            {
                ++cursor;
            }
            if (cursor >= tagText.size() || tagText.at(cursor) != QLatin1Char('='))
            {
                continue;
            }
            ++cursor;

            QString literal;
            bool quoted = false;
            if (!readAttributeLiteral(tagText, &cursor, &literal, &quoted))
            {
                continue;
            }

            QString valueKind;
            attributes.append({
                attributeName,
                parseAttributeValue(literal, quoted, &valueKind),
                valueKind,
                literal
            });
        }
        return attributes;
    }

    QVariantList attributeListForResult(const QList<ParsedAttribute>& attributes)
    {
        QVariantList result;
        result.reserve(attributes.size());
        for (const ParsedAttribute& attribute : attributes)
        {
            QVariantMap item;
            item.insert(QStringLiteral("name"), attribute.name);
            item.insert(QStringLiteral("value"), attribute.value);
            item.insert(QStringLiteral("valueKind"), attribute.valueKind);
            item.insert(QStringLiteral("literal"), attribute.literal);
            result.append(item);
        }
        return result;
    }

    QVariantMap invalidResult(const QString& sourceText, const QString& errorMessage)
    {
        QVariantMap result;
        result.insert(QStringLiteral("valid"), false);
        result.insert(QStringLiteral("bodySourceText"), sourceText);
        result.insert(QStringLiteral("tagName"), QString());
        result.insert(QStringLiteral("properties"), QVariantMap());
        result.insert(QStringLiteral("valueKinds"), QVariantMap());
        result.insert(QStringLiteral("attributes"), QVariantList());
        result.insert(QStringLiteral("propertyCount"), 0);
        result.insert(QStringLiteral("tagStart"), -1);
        result.insert(QStringLiteral("tagEnd"), -1);
        result.insert(QStringLiteral("errorMessage"), errorMessage);
        return result;
    }

    QVariantMap readPropertiesFromSourceText(const QString& bodySourceText, const int tagPosition)
    {
        const QString sourceText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodySourceText);
        const TagRange sourceTagRange = tagRangeAtPosition(sourceText, tagPosition);
        if (!sourceTagRange.isValid())
        {
            return invalidResult(
                sourceText,
                QStringLiteral("No editable tag was found at the requested position."));
        }

        const QString tagText = sourceText.mid(sourceTagRange.start, sourceTagRange.end - sourceTagRange.start + 1);
        const QList<ParsedAttribute> attributes = attributesFromTagText(tagText);
        QVariantMap properties;
        QVariantMap valueKinds;
        for (const ParsedAttribute& attribute : attributes)
        {
            properties.insert(attribute.name, attribute.value);
            valueKinds.insert(attribute.name, attribute.valueKind);
        }

        QVariantMap result;
        result.insert(QStringLiteral("valid"), true);
        result.insert(QStringLiteral("bodySourceText"), sourceText);
        result.insert(QStringLiteral("tagName"), tagNameFromTagText(tagText));
        result.insert(QStringLiteral("properties"), properties);
        result.insert(QStringLiteral("valueKinds"), valueKinds);
        result.insert(QStringLiteral("attributes"), attributeListForResult(attributes));
        result.insert(QStringLiteral("propertyCount"), properties.size());
        result.insert(QStringLiteral("tagStart"), sourceTagRange.start);
        result.insert(QStringLiteral("tagEnd"), sourceTagRange.end);
        result.insert(QStringLiteral("errorMessage"), QString());
        return result;
    }
} // namespace

GetProperty::GetProperty(QObject* parent)
    : QObject(parent)
{
}

QString GetProperty::tagName() const
{
    return m_tagName;
}

QVariantMap GetProperty::properties() const
{
    return m_properties;
}

QVariantMap GetProperty::valueKinds() const
{
    return m_valueKinds;
}

int GetProperty::propertyCount() const
{
    return m_properties.size();
}

QString GetProperty::lastError() const
{
    return m_lastError;
}

QVariantMap GetProperty::readPropertiesFromSource(const QString& bodySourceText, const int tagPosition)
{
    QVariantMap result = readPropertiesFromSourceText(bodySourceText, tagPosition);
    if (!result.value(QStringLiteral("valid")).toBool())
    {
        applyCapturedProperties(QString(), QVariantMap(), QVariantMap());
        updateLastError(result.value(QStringLiteral("errorMessage")).toString());
        return result;
    }

    applyCapturedProperties(
        result.value(QStringLiteral("tagName")).toString(),
        result.value(QStringLiteral("properties")).toMap(),
        result.value(QStringLiteral("valueKinds")).toMap());
    clearLastError();
    emit propertiesCaptured(result);
    return result;
}

QVariantMap GetProperty::readPropertiesFromBodyDocument(
    const QString& bodyDocumentText,
    const int tagPosition)
{
    const QString sourceText = bodyDocumentText.trimmed().isEmpty()
        ? QString()
        : ThinkingSpace::NoteBodyPersistence::sourceTextFromBodyDocument(bodyDocumentText);

    QVariantMap result = readPropertiesFromSourceText(sourceText, tagPosition);
    result.insert(QStringLiteral("bodyDocumentText"), bodyDocumentText);
    if (!result.value(QStringLiteral("valid")).toBool())
    {
        applyCapturedProperties(QString(), QVariantMap(), QVariantMap());
        updateLastError(result.value(QStringLiteral("errorMessage")).toString());
        return result;
    }

    applyCapturedProperties(
        result.value(QStringLiteral("tagName")).toString(),
        result.value(QStringLiteral("properties")).toMap(),
        result.value(QStringLiteral("valueKinds")).toMap());
    clearLastError();
    emit propertiesCaptured(result);
    return result;
}

bool GetProperty::containsProperty(const QString& propertyName) const
{
    return m_properties.contains(normalizedPropertyName(propertyName));
}

QVariant GetProperty::propertyValue(const QString& propertyName) const
{
    return m_properties.value(normalizedPropertyName(propertyName));
}

void GetProperty::clearProperties()
{
    applyCapturedProperties(QString(), QVariantMap(), QVariantMap());
}

void GetProperty::clearLastError()
{
    updateLastError(QString());
}

void GetProperty::applyCapturedProperties(
    const QString& tagName,
    const QVariantMap& properties,
    const QVariantMap& valueKinds)
{
    const bool shouldEmitTagNameChanged = m_tagName != tagName;
    const bool shouldEmitPropertiesChanged = m_properties != properties || m_valueKinds != valueKinds;

    if (!shouldEmitTagNameChanged && !shouldEmitPropertiesChanged)
    {
        return;
    }

    m_tagName = tagName;
    m_properties = properties;
    m_valueKinds = valueKinds;

    if (shouldEmitTagNameChanged)
    {
        emit tagNameChanged();
    }
    if (shouldEmitPropertiesChanged)
    {
        emit propertiesChanged();
    }
}

void GetProperty::updateLastError(const QString& message)
{
    if (m_lastError == message)
    {
        return;
    }

    m_lastError = message;
    emit lastErrorChanged();
}
