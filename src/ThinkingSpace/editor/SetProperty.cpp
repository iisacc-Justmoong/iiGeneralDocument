#include "ThinkingSpace/editor/SetProperty.h"

#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"

#include <QMetaType>
#include <QLocale>

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

    struct SerializedPropertyValue
    {
        bool valid = false;
        QString literal;
        QString kind;
        QString errorMessage;
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

    bool isValidPropertyName(const QString& propertyName)
    {
        if (propertyName.isEmpty() || !isNameStartCharacter(propertyName.front()))
        {
            return false;
        }

        for (const QChar ch : propertyName)
        {
            if (!isNameCharacter(ch))
            {
                return false;
            }
        }
        return true;
    }

    QString escapeXmlAttributeValue(QString value)
    {
        value.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
        value.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
        value.replace(QStringLiteral("'"), QStringLiteral("&apos;"));
        value.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
        value.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
        return value;
    }

    bool isSignedIntegerType(const int metaTypeId)
    {
        return metaTypeId == QMetaType::SChar
            || metaTypeId == QMetaType::Short
            || metaTypeId == QMetaType::Int
            || metaTypeId == QMetaType::Long
            || metaTypeId == QMetaType::LongLong;
    }

    bool isUnsignedIntegerType(const int metaTypeId)
    {
        return metaTypeId == QMetaType::UChar
            || metaTypeId == QMetaType::UShort
            || metaTypeId == QMetaType::UInt
            || metaTypeId == QMetaType::ULong
            || metaTypeId == QMetaType::ULongLong;
    }

    SerializedPropertyValue serializePropertyValue(const QVariant& value)
    {
        if (!value.isValid() || value.isNull())
        {
            return {
                false,
                QString(),
                QString(),
                QStringLiteral("Property value is empty.")
            };
        }

        const int metaTypeId = value.metaType().id();
        if (metaTypeId == QMetaType::Bool)
        {
            return {
                true,
                value.toBool() ? QStringLiteral("true") : QStringLiteral("false"),
                QStringLiteral("bool"),
                QString()
            };
        }

        if (isSignedIntegerType(metaTypeId))
        {
            bool ok = false;
            const qlonglong number = value.toLongLong(&ok);
            return {
                ok,
                ok ? QString::number(number) : QString(),
                QStringLiteral("int"),
                ok ? QString() : QStringLiteral("Integer property value could not be serialized.")
            };
        }

        if (isUnsignedIntegerType(metaTypeId))
        {
            bool ok = false;
            const qulonglong number = value.toULongLong(&ok);
            return {
                ok,
                ok ? QString::number(number) : QString(),
                QStringLiteral("int"),
                ok ? QString() : QStringLiteral("Integer property value could not be serialized.")
            };
        }

        if (metaTypeId == QMetaType::Float || metaTypeId == QMetaType::Double)
        {
            bool ok = false;
            const double number = value.toDouble(&ok);
            if (!ok || !std::isfinite(number))
            {
                return {
                    false,
                    QString(),
                    QStringLiteral("float"),
                    QStringLiteral("Float property value could not be serialized.")
                };
            }
            return {
                true,
                QLocale::c().toString(number, 'g', 16),
                QStringLiteral("float"),
                QString()
            };
        }

        if (metaTypeId == QMetaType::QString || metaTypeId == QMetaType::QByteArray || metaTypeId == QMetaType::QChar)
        {
            return {
                true,
                QStringLiteral("\"%1\"").arg(escapeXmlAttributeValue(value.toString())),
                QStringLiteral("string"),
                QString()
            };
        }

        return {
            false,
            QString(),
            QString(),
            QStringLiteral("Unsupported property value type.")
        };
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

    int tagNameEndOffset(const QString& tagText)
    {
        int cursor = 1;
        while (cursor < tagText.size() && tagText.at(cursor).isSpace())
        {
            ++cursor;
        }
        while (cursor < tagText.size() && isNameCharacter(tagText.at(cursor)))
        {
            ++cursor;
        }
        return cursor;
    }

    int selfClosingSlashOffset(const QString& tagText)
    {
        int cursor = tagText.size() - 2;
        while (cursor >= 0 && tagText.at(cursor).isSpace())
        {
            --cursor;
        }
        return cursor >= 0 && tagText.at(cursor) == QLatin1Char('/') ? cursor : -1;
    }

    int insertionOffsetForAttribute(const QString& tagText)
    {
        const int slashOffset = selfClosingSlashOffset(tagText);
        if (slashOffset >= 0)
        {
            int cursor = slashOffset - 1;
            while (cursor >= 0 && tagText.at(cursor).isSpace())
            {
                --cursor;
            }
            return cursor + 1;
        }
        return tagText.size() - 1;
    }

    int skipAttributeValue(const QString& tagText, int cursor)
    {
        while (cursor < tagText.size() && tagText.at(cursor).isSpace())
        {
            ++cursor;
        }

        if (cursor >= tagText.size())
        {
            return cursor;
        }

        const QChar quote = tagText.at(cursor);
        if (quote == QLatin1Char('"') || quote == QLatin1Char('\''))
        {
            ++cursor;
            while (cursor < tagText.size() && tagText.at(cursor) != quote)
            {
                ++cursor;
            }
            return cursor < tagText.size() ? cursor + 1 : cursor;
        }

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

    TagRange attributeRange(const QString& tagText, const QString& propertyName)
    {
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

            const int nameStart = cursor;
            if (!isNameStartCharacter(tagText.at(cursor)))
            {
                ++cursor;
                continue;
            }
            ++cursor;
            while (cursor < tagText.size() && isNameCharacter(tagText.at(cursor)))
            {
                ++cursor;
            }
            const int nameEnd = cursor;
            while (cursor < tagText.size() && tagText.at(cursor).isSpace())
            {
                ++cursor;
            }
            if (cursor < tagText.size() && tagText.at(cursor) == QLatin1Char('='))
            {
                ++cursor;
                cursor = skipAttributeValue(tagText, cursor);
            }

            if (tagText.mid(nameStart, nameEnd - nameStart) == propertyName)
            {
                return { nameStart, cursor - 1 };
            }
        }
        return {};
    }

    QVariantMap invalidResult(
        const QString& sourceText,
        const QString& propertyName,
        const QString& errorMessage)
    {
        QVariantMap result;
        result.insert(QStringLiteral("valid"), false);
        result.insert(QStringLiteral("changed"), false);
        result.insert(QStringLiteral("bodySourceText"), sourceText);
        result.insert(QStringLiteral("propertyName"), propertyName);
        result.insert(QStringLiteral("propertyValue"), QString());
        result.insert(QStringLiteral("valueKind"), QString());
        result.insert(QStringLiteral("serializedAttribute"), QString());
        result.insert(QStringLiteral("tagStart"), -1);
        result.insert(QStringLiteral("tagEnd"), -1);
        result.insert(QStringLiteral("errorMessage"), errorMessage);
        return result;
    }

    QVariantMap setPropertyInSourceText(
        const QString& bodySourceText,
        const int tagPosition,
        const QString& rawPropertyName,
        const QVariant& value)
    {
        const QString sourceText = ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(bodySourceText);
        const QString propertyName = normalizedPropertyName(rawPropertyName);
        if (!isValidPropertyName(propertyName))
        {
            return invalidResult(
                sourceText,
                propertyName,
                QStringLiteral("Invalid property name: %1").arg(rawPropertyName));
        }

        const SerializedPropertyValue serializedValue = serializePropertyValue(value);
        if (!serializedValue.valid)
        {
            return invalidResult(sourceText, propertyName, serializedValue.errorMessage);
        }

        const TagRange sourceTagRange = tagRangeAtPosition(sourceText, tagPosition);
        if (!sourceTagRange.isValid())
        {
            return invalidResult(
                sourceText,
                propertyName,
                QStringLiteral("No editable tag was found at the requested position."));
        }

        const QString serializedAttribute =
            QStringLiteral("%1=%2").arg(propertyName, serializedValue.literal);
        QString tagText = sourceText.mid(sourceTagRange.start, sourceTagRange.end - sourceTagRange.start + 1);
        const TagRange existingAttributeRange = attributeRange(tagText, propertyName);
        if (existingAttributeRange.isValid())
        {
            tagText.replace(
                existingAttributeRange.start,
                existingAttributeRange.end - existingAttributeRange.start + 1,
                serializedAttribute);
        }
        else
        {
            tagText.insert(insertionOffsetForAttribute(tagText), QStringLiteral(" ") + serializedAttribute);
        }

        QString mutatedSourceText = sourceText;
        mutatedSourceText.replace(
            sourceTagRange.start,
            sourceTagRange.end - sourceTagRange.start + 1,
            tagText);

        QVariantMap result;
        result.insert(QStringLiteral("valid"), true);
        result.insert(QStringLiteral("changed"), mutatedSourceText != sourceText);
        result.insert(QStringLiteral("bodySourceText"), mutatedSourceText);
        result.insert(QStringLiteral("propertyName"), propertyName);
        result.insert(QStringLiteral("propertyValue"), serializedValue.literal);
        result.insert(QStringLiteral("valueKind"), serializedValue.kind);
        result.insert(QStringLiteral("serializedAttribute"), serializedAttribute);
        result.insert(QStringLiteral("tagStart"), sourceTagRange.start);
        result.insert(QStringLiteral("tagEnd"), sourceTagRange.start + tagText.size() - 1);
        result.insert(QStringLiteral("errorMessage"), QString());
        return result;
    }
} // namespace

SetProperty::SetProperty(QObject* parent)
    : QObject(parent)
{
}

QString SetProperty::lastError() const
{
    return m_lastError;
}

QVariantMap SetProperty::setPropertyInSource(
    const QString& bodySourceText,
    const int tagPosition,
    const QString& propertyName,
    const QVariant& value)
{
    QVariantMap result = setPropertyInSourceText(bodySourceText, tagPosition, propertyName, value);
    if (!result.value(QStringLiteral("valid")).toBool())
    {
        updateLastError(result.value(QStringLiteral("errorMessage")).toString());
        return result;
    }

    clearLastError();
    emit propertySet(result);
    return result;
}

QVariantMap SetProperty::setPropertyInBodyDocument(
    const QString& noteId,
    const QString& bodyDocumentText,
    const int tagPosition,
    const QString& propertyName,
    const QVariant& value)
{
    const QString sourceText = bodyDocumentText.trimmed().isEmpty()
        ? QString()
        : ThinkingSpace::NoteBodyPersistence::sourceTextFromBodyDocument(bodyDocumentText);

    QVariantMap result = setPropertyInSourceText(sourceText, tagPosition, propertyName, value);
    if (!result.value(QStringLiteral("valid")).toBool())
    {
        updateLastError(result.value(QStringLiteral("errorMessage")).toString());
        result.insert(QStringLiteral("bodyDocumentText"), bodyDocumentText);
        return result;
    }

    result.insert(
        QStringLiteral("bodyDocumentText"),
        ThinkingSpace::NoteBodyPersistence::serializeBodyDocument(
            noteId,
            result.value(QStringLiteral("bodySourceText")).toString()));
    clearLastError();
    emit propertySet(result);
    return result;
}

void SetProperty::clearLastError()
{
    updateLastError(QString());
}

void SetProperty::updateLastError(const QString& message)
{
    if (m_lastError == message)
    {
        return;
    }

    m_lastError = message;
    emit lastErrorChanged();
}
