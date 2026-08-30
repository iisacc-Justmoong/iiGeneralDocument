#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>
#include <QRegularExpression>

namespace ThinkingSpace::NoteFolders
{
    inline QString escapeFolderPathSegment(QString value)
    {
        value = value.trimmed();
        value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        value.replace(QLatin1Char('/'), QStringLiteral("\\/"));
        return value;
    }

    inline QStringList folderPathSegments(QString value)
    {
        value = value.trimmed();
        if (value.isEmpty())
        {
            return {};
        }

        QStringList segments;
        QString currentSegment;
        currentSegment.reserve(value.size());

        auto flushCurrentSegment = [&segments, &currentSegment]()
        {
            const QString normalizedSegment = currentSegment.trimmed();
            currentSegment.clear();
            if (!normalizedSegment.isEmpty())
            {
                segments.push_back(normalizedSegment);
            }
        };

        for (int index = 0; index < value.size(); ++index)
        {
            const QChar character = value.at(index);
            if (character == QLatin1Char('\\'))
            {
                const bool hasNextCharacter = index + 1 < value.size();
                if (hasNextCharacter)
                {
                    const QChar nextCharacter = value.at(index + 1);
                    if (nextCharacter == QLatin1Char('\\') || nextCharacter == QLatin1Char('/'))
                    {
                        currentSegment.push_back(nextCharacter);
                        ++index;
                        continue;
                    }
                }

                flushCurrentSegment();
                continue;
            }

            if (character == QLatin1Char('/'))
            {
                flushCurrentSegment();
                continue;
            }

            currentSegment.push_back(character);
        }

        flushCurrentSegment();
        return segments;
    }

    inline QString joinFolderPathSegments(const QStringList& segments)
    {
        QStringList sanitizedSegments;
        sanitizedSegments.reserve(segments.size());

        for (QString segment : segments)
        {
            segment = segment.trimmed();
            if (segment.isEmpty())
            {
                continue;
            }
            sanitizedSegments.push_back(escapeFolderPathSegment(std::move(segment)));
        }

        return sanitizedSegments.join(QLatin1Char('/'));
    }

    inline QString normalizeFolderPath(QString value)
    {
        return joinFolderPathSegments(folderPathSegments(std::move(value)));
    }

    inline QString appendFolderPathSegment(const QString& parentPath, QString segment)
    {
        segment = segment.trimmed();
        if (segment.isEmpty())
        {
            return normalizeFolderPath(parentPath);
        }

        QStringList segments = folderPathSegments(parentPath);
        segments.push_back(segment);
        return joinFolderPathSegments(segments);
    }

    inline QString displayFolderPath(QString value)
    {
        return folderPathSegments(std::move(value)).join(QLatin1Char('/'));
    }

    inline bool isHierarchicalFolderPath(const QString& value)
    {
        return folderPathSegments(value).size() > 1;
    }

    inline QString leafFolderName(QString value)
    {
        const QStringList segments = folderPathSegments(std::move(value));
        if (segments.isEmpty())
        {
            return {};
        }

        return segments.isEmpty() ? QString() : segments.constLast().trimmed();
    }

    inline bool usesReservedTodayFolderSegment(const QString& value)
    {
        const QStringList segments = folderPathSegments(value);
        if (segments.isEmpty())
        {
            return false;
        }

        for (const QString& segment : segments)
        {
            if (segment.trimmed().toCaseFolded() == QStringLiteral("today"))
            {
                return true;
            }
        }

        return false;
    }

    struct RawFoldersBlockState final
    {
        bool blockPresent = false;
        bool hasConcreteEntry = false;
    };

    inline RawFoldersBlockState inspectRawFoldersBlock(const QString& tsnHeadText)
    {
        static const QRegularExpression foldersBlockRegex(
            QStringLiteral(R"(<\s*folders\b[^>]*>([\s\S]*?)<\s*/\s*folders\s*>)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression folderTagRegex(
            QStringLiteral(R"(<\s*folder\b[^>]*>([\s\S]*?)<\s*/\s*folder\s*>)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression xmlTagRegex(QStringLiteral(R"(<[^>]+>)"));

        RawFoldersBlockState state;
        const QRegularExpressionMatch foldersMatch = foldersBlockRegex.match(tsnHeadText);
        if (!foldersMatch.hasMatch())
        {
            return state;
        }

        state.blockPresent = true;
        QString foldersInnerText = foldersMatch.captured(1);

        QRegularExpressionMatchIterator folderIt = folderTagRegex.globalMatch(foldersInnerText);
        while (folderIt.hasNext())
        {
            const QRegularExpressionMatch folderMatch = folderIt.next();
            if (!folderMatch.captured(1).trimmed().isEmpty())
            {
                state.hasConcreteEntry = true;
                return state;
            }
        }

        foldersInnerText.remove(folderTagRegex);
        foldersInnerText.remove(xmlTagRegex);
        state.hasConcreteEntry = !foldersInnerText.trimmed().isEmpty();
        return state;
    }
} // namespace ThinkingSpace::NoteFolders

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
