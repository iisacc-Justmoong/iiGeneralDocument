#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

#include <array>

namespace ThinkingSpace::Bookmarks
{
    struct BookmarkColorDefinition final
    {
        const char* name;
        const char* displayName;
        const char* hex;
    };

    inline constexpr std::array<BookmarkColorDefinition, 10> kBookmarkColorDefinitions{
        {
            {"red", "Red", "#EF4444"},
            {"orange", "Orange", "#F97316"},
            {"amber", "Amber", "#F59E0B"},
            {"yellow", "Yellow", "#EAB308"},
            {"green", "Green", "#22C55E"},
            {"teal", "Teal", "#14B8A6"},
            {"blue", "Blue", "#3B82F6"},
            {"indigo", "Indigo", "#B589EC"},
            {"purple", "Purple", "#8B5CF6"},
            {"pink", "Pink", "#EC4899"}
        }
    };

    inline QString defaultBookmarkColorHex()
    {
        return QString::fromLatin1(kBookmarkColorDefinitions.at(2).hex); // amber
    }

    inline QString normalizeBookmarkColorName(QString value)
    {
        return value.trimmed().toCaseFolded();
    }

    inline bool isHexColor(const QString& value)
    {
        if (!value.startsWith(QLatin1Char('#')))
        {
            return false;
        }
        const QString hex = value.mid(1);
        if (hex.size() != 3 && hex.size() != 6)
        {
            return false;
        }

        for (const QChar ch : hex)
        {
            const bool isDigit = ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
            const bool isLowerHex = ch >= QLatin1Char('a') && ch <= QLatin1Char('f');
            const bool isUpperHex = ch >= QLatin1Char('A') && ch <= QLatin1Char('F');
            if (!isDigit && !isLowerHex && !isUpperHex)
            {
                return false;
            }
        }

        return true;
    }

    inline QString normalizeHexColor(QString value)
    {
        value = value.trimmed();
        if (!isHexColor(value))
        {
            return {};
        }

        QString hex = value.mid(1).toUpper();
        if (hex.size() == 3)
        {
            QString expanded;
            expanded.reserve(6);
            for (const QChar ch : hex)
            {
                expanded.push_back(ch);
                expanded.push_back(ch);
            }
            hex = expanded;
        }
        return QStringLiteral("#") + hex;
    }

    inline QString bookmarkColorHexForName(const QString& value)
    {
        const QString normalized = normalizeBookmarkColorName(value);
        for (const BookmarkColorDefinition& definition : kBookmarkColorDefinitions)
        {
            if (normalized == QString::fromLatin1(definition.name))
            {
                return QString::fromLatin1(definition.hex);
            }
        }
        return {};
    }

    inline QString bookmarkDisplayNameForName(const QString& value)
    {
        const QString normalized = normalizeBookmarkColorName(value);
        for (const BookmarkColorDefinition& definition : kBookmarkColorDefinitions)
        {
            if (normalized == QString::fromLatin1(definition.name))
            {
                return QString::fromLatin1(definition.displayName);
            }
        }
        return {};
    }

    inline QString bookmarkColorToHex(const QString& value)
    {
        const QString trimmed = value.trimmed();
        if (trimmed.isEmpty())
        {
            return defaultBookmarkColorHex();
        }

        if (isHexColor(trimmed))
        {
            return normalizeHexColor(trimmed);
        }

        const QString mappedHex = bookmarkColorHexForName(trimmed);
        if (!mappedHex.isEmpty())
        {
            return mappedHex;
        }

        return defaultBookmarkColorHex();
    }

    inline QString canonicalBookmarkColorToken(const QString& value)
    {
        const QString trimmed = value.trimmed();
        if (trimmed.isEmpty())
        {
            return {};
        }

        if (isHexColor(trimmed))
        {
            return normalizeHexColor(trimmed);
        }

        const QString normalized = normalizeBookmarkColorName(trimmed);
        for (const BookmarkColorDefinition& definition : kBookmarkColorDefinitions)
        {
            if (normalized == QString::fromLatin1(definition.name))
            {
                return QString::fromLatin1(definition.name);
            }
        }

        return normalized;
    }

    inline QStringList parseBookmarkColorsAttribute(const QString& rawText)
    {
        QString value = rawText.trimmed();
        if (value.isEmpty())
        {
            return {};
        }

        if ((value.startsWith(QLatin1Char('{')) && value.endsWith(QLatin1Char('}')))
            || (value.startsWith(QLatin1Char('[')) && value.endsWith(QLatin1Char(']'))))
        {
            value = value.mid(1, value.size() - 2).trimmed();
        }

        QStringList colors;
        const QStringList tokens = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        colors.reserve(tokens.size());

        for (QString token : tokens)
        {
            token = token.trimmed();
            if ((token.startsWith(QLatin1Char('"')) && token.endsWith(QLatin1Char('"')) && token.size() >= 2)
                || (token.startsWith(QLatin1Char('\'')) && token.endsWith(QLatin1Char('\'')) && token.size() >= 2))
            {
                token = token.mid(1, token.size() - 2).trimmed();
            }

            const QString canonical = canonicalBookmarkColorToken(token);
            if (canonical.isEmpty() || colors.contains(canonical))
            {
                continue;
            }
            colors.push_back(canonical);
        }

        return colors;
    }

    inline QString serializeBookmarkColorsAttribute(const QStringList& colors)
    {
        QStringList sanitized;
        sanitized.reserve(colors.size());

        for (const QString& color : colors)
        {
            const QString canonical = canonicalBookmarkColorToken(color);
            if (canonical.isEmpty() || sanitized.contains(canonical))
            {
                continue;
            }
            sanitized.push_back(canonical);
        }

        if (sanitized.isEmpty())
        {
            return {};
        }

        return QStringLiteral("{%1}").arg(sanitized.join(QStringLiteral(",")));
    }
} // namespace ThinkingSpace::Bookmarks

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
