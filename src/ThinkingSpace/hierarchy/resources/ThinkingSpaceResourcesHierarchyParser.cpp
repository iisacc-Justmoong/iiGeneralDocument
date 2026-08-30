#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcesHierarchyParser.hpp"

#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcesHierarchyStore.hpp"
#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

namespace
{
    QString extractPathValueFromObject(
        const QJsonObject& object,
        const QStringList& candidateKeys = QStringList{
            QStringLiteral("resourcePath"),
            QStringLiteral("path"),
            QStringLiteral("resource"),
            QStringLiteral("value")})
    {
        for (const QString& objectKey : object.keys())
        {
            for (const QString& candidateKey : candidateKeys)
            {
                if (objectKey.compare(candidateKey, Qt::CaseInsensitive) != 0)
                {
                    continue;
                }

                const QJsonValue value = object.value(objectKey);
                if (!value.isString())
                {
                    continue;
                }

                const QString text = value.toString().trimmed();
                if (!text.isEmpty())
                {
                    return text;
                }
            }
        }

        return {};
    }

    QString extractResourcePathAttribute(const QString& resourceTagText)
    {
        const QRegularExpression quotedDouble(
            QStringLiteral("(?:resourcePath|path)\\s*=\\s*\"([^\"]+)\""),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch quotedDoubleMatch = quotedDouble.match(resourceTagText);
        if (quotedDoubleMatch.hasMatch())
        {
            return quotedDoubleMatch.captured(1).trimmed();
        }

        const QRegularExpression quotedSingle(
            QStringLiteral("(?:resourcePath|path)\\s*=\\s*'([^']+)'"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch quotedSingleMatch = quotedSingle.match(resourceTagText);
        if (quotedSingleMatch.hasMatch())
        {
            return quotedSingleMatch.captured(1).trimmed();
        }

        const QRegularExpression bare(
            QStringLiteral("(?:resourcePath|path)\\s*=\\s*([^\\s\"'/>]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch bareMatch = bare.match(resourceTagText);
        if (bareMatch.hasMatch())
        {
            return bareMatch.captured(1).trimmed();
        }

        return {};
    }

    QStringList parseResourceTagValues(const QString& rawText)
    {
        QStringList values;
        const QRegularExpression resourceTag(
            QStringLiteral(R"(<\s*resource\b[^>]*>)"),
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator iterator = resourceTag.globalMatch(rawText);
        while (iterator.hasNext())
        {
            const QRegularExpressionMatch match = iterator.next();
            const QString parsedPath = extractResourcePathAttribute(match.captured(0));
            if (!parsedPath.isEmpty())
            {
                values.push_back(parsedPath);
            }
        }
        return values;
    }

    QStringList sanitizeLines(const QString& rawText)
    {
        QStringList values;
        const QStringList lines = rawText.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
        values.reserve(lines.size());

        for (QString line : lines)
        {
            line = line.trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            {
                continue;
            }
            values.push_back(line);
        }

        return values;
    }

    QStringList parseArrayValues(const QJsonArray& array)
    {
        QStringList values;
        values.reserve(array.size());

        for (const QJsonValue& value : array)
        {
            if (value.isString())
            {
                const QString text = value.toString().trimmed();
                if (!text.isEmpty())
                {
                    values.push_back(text);
                }
            }
            else if (value.isObject())
            {
                const QJsonObject object = value.toObject();
                const QString text = extractPathValueFromObject(object);
                if (!text.isEmpty())
                {
                    values.push_back(text);
                }
            }
        }

        return values;
    }

    QStringList parseLineValues(const QString& rawText)
    {
        QStringList values;
        const QStringList lines = sanitizeLines(rawText);
        values.reserve(lines.size());
        for (const QString& line : lines)
        {
            const QString parsedPath = extractResourcePathAttribute(line);
            if (!parsedPath.isEmpty())
            {
                values.push_back(parsedPath);
                continue;
            }

            // Ignore XML-like wrappers such as <resources> ... </resources>.
            if (line.startsWith(QLatin1Char('<')) && line.endsWith(QLatin1Char('>')))
            {
                continue;
            }
            values.push_back(line);
        }
        return values;
    }
} // namespace

ThinkingSpaceResourcesHierarchyParser::ThinkingSpaceResourcesHierarchyParser() = default;

ThinkingSpaceResourcesHierarchyParser::~ThinkingSpaceResourcesHierarchyParser() = default;

bool ThinkingSpaceResourcesHierarchyParser::parse(
    const QString& rawText,
    ThinkingSpaceResourcesHierarchyStore* outStore,
    QString* errorMessage) const
{
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hierarchy.resources.parser"),
                              QStringLiteral("parse.begin"),
                              QStringLiteral("bytes=%1").arg(rawText.toUtf8().size()));

    if (outStore == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("outStore must not be null.");
        }
        return false;
    }

    outStore->clear();

    if (rawText.trimmed().isEmpty())
    {
        outStore->setResourcePaths({});
        ThinkingSpace::Debug::traceSelf(this,
                                  QStringLiteral("hierarchy.resources.parser"),
                                  QStringLiteral("parse.empty"));
        return true;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawText.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && !document.isNull())
    {
        bool recognizedJsonForm = false;
        QStringList parsedValues;
        if (document.isArray())
        {
            recognizedJsonForm = true;
            parsedValues = parseArrayValues(document.array());
        }
        else if (document.isObject())
        {
            const QJsonObject object = document.object();
            const QJsonValue listValue = object.value(QStringLiteral("resources"));
            if (listValue.isArray())
            {
                recognizedJsonForm = true;
                parsedValues = parseArrayValues(listValue.toArray());
            }
            else if (listValue.isString())
            {
                recognizedJsonForm = true;
                parsedValues = QStringList{listValue.toString().trimmed()};
            }
            else
            {
                const QString directPathValue = extractPathValueFromObject(object);
                if (!directPathValue.isEmpty())
                {
                    recognizedJsonForm = true;
                    parsedValues = QStringList{directPathValue};
                }
            }
        }

        if (recognizedJsonForm)
        {
            outStore->setResourcePaths(parsedValues);
            return true;
        }
    }

    const QStringList tagValues = parseResourceTagValues(rawText);
    if (!tagValues.isEmpty())
    {
        outStore->setResourcePaths(tagValues);
        return true;
    }

    outStore->setResourcePaths(parseLineValues(rawText));
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hierarchy.resources.parser"),
                              QStringLiteral("parse.fallbackLines"),
                              QStringLiteral("count=%1").arg(outStore->resourcePaths().size()));
    return true;
}
