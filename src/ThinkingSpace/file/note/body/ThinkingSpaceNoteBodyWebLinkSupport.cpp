#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyWebLinkSupport.hpp"

#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace
{
    const QRegularExpression kAttributePattern(
        QStringLiteral(R"ATTR(\bhref\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))ATTR"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression kPlainWebLinkPattern(
        QStringLiteral(R"((?:https?://|www\.)[^\s<>'"]+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression kTagPattern(
        QStringLiteral(R"(<\s*/?\s*([A-Za-z_][A-Za-z0-9_.:-]*)\b[^>]*>)"));
    const QRegularExpression kSchemeUrlPattern(
        QStringLiteral(R"(^https?://[^\s]+$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression kWwwUrlPattern(
        QStringLiteral(R"(^www\.[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+(?:[/?#][^\s]*)?$)"),
        QRegularExpression::CaseInsensitiveOption);

    QString normalizeLineEndings(QString text)
    {
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        text.replace(QChar::LineSeparator, QLatin1Char('\n'));
        text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        return text;
    }

    QString decodeXmlEntities(QString text)
    {
        text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
        text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
        text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
        text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        return text;
    }

    QString escapeXmlAttributeValue(QString value)
    {
        value.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
        value.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
        value.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
        value.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
        value.replace(QStringLiteral("'"), QStringLiteral("&apos;"));
        return value;
    }

    QString escapeHtmlAttributeValue(QString value)
    {
        value.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
        value.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
        value.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
        value.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
        value.replace(QStringLiteral("'"), QStringLiteral("&#39;"));
        return value;
    }

    QString normalizedTagName(const QString& elementName)
    {
        return elementName.trimmed().toCaseFolded();
    }

    bool isClosingTagToken(const QString& token)
    {
        for (int index = 1; index < token.size(); ++index)
        {
            const QChar ch = token.at(index);
            if (!ch.isSpace())
            {
                return ch == QLatin1Char('/');
            }
        }
        return false;
    }

    bool isSelfClosingTagToken(QString token)
    {
        token = token.trimmed();
        return token.endsWith(QStringLiteral("/>"));
    }

    QString hrefAttributeFromRawToken(const QString& rawTagText)
    {
        const QRegularExpressionMatch match = kAttributePattern.match(rawTagText);
        if (!match.hasMatch())
        {
            return {};
        }

        for (int captureIndex = 1; captureIndex <= 3; ++captureIndex)
        {
            if (match.capturedStart(captureIndex) < 0)
            {
                continue;
            }
            return decodeXmlEntities(match.captured(captureIndex)).trimmed();
        }
        return {};
    }

    bool isTrailingUrlPunctuation(const QChar ch)
    {
        switch (ch.unicode())
        {
        case '.':
        case ',':
        case ':':
        case ';':
        case '!':
        case '?':
        case ')':
        case ']':
        case '}':
        case '>':
        case '"':
        case '\'':
        case 0x3001:
        case 0x3002:
        case 0xFF0C:
        case 0xFF01:
        case 0xFF1F:
        case 0x300D:
        case 0x300F:
        case 0x3011:
            return true;
        default:
            return false;
        }
    }

    bool isWordLikeUrlBoundaryCharacter(const QChar ch)
    {
        return ch.isLetterOrNumber()
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('@');
    }

    bool isStartBoundary(const QString& text, const qsizetype start)
    {
        if (start <= 0 || start > text.size())
        {
            return true;
        }
        return !isWordLikeUrlBoundaryCharacter(text.at(start - 1));
    }

    bool isMarkdownLinkDestinationContext(const QString& text, const qsizetype start)
    {
        if (start <= 0 || start > text.size() || text.at(start - 1) != QLatin1Char('('))
        {
            return false;
        }

        qsizetype cursor = start - 1;
        while (cursor > 0)
        {
            --cursor;
            if (text.at(cursor).isSpace())
            {
                continue;
            }
            return text.at(cursor) == QLatin1Char(']');
        }
        return false;
    }

    bool isDetectableWebLinkCandidate(const QString& candidate)
    {
        const QString normalizedCandidate = decodeXmlEntities(candidate).trimmed();
        if (normalizedCandidate.isEmpty())
        {
            return false;
        }
        return kSchemeUrlPattern.match(normalizedCandidate).hasMatch()
            || kWwwUrlPattern.match(normalizedCandidate).hasMatch();
    }

    QString wrapDetectedWebLinksInLiteralSegment(const QString& textSegment)
    {
        if (textSegment.isEmpty())
        {
            return {};
        }

        QString output;
        qsizetype cursor = 0;
        QRegularExpressionMatchIterator iterator = kPlainWebLinkPattern.globalMatch(textSegment);
        while (iterator.hasNext())
        {
            const QRegularExpressionMatch match = iterator.next();
            const qsizetype matchStart = match.capturedStart(0);
            const qsizetype matchEnd = match.capturedEnd(0);
            if (matchStart < cursor || matchStart < 0 || matchEnd <= matchStart)
            {
                continue;
            }

            QString rawToken = match.captured(0);
            qsizetype trimmedLength = rawToken.size();
            while (trimmedLength > 0 && isTrailingUrlPunctuation(rawToken.at(trimmedLength - 1)))
            {
                --trimmedLength;
            }

            const QString candidate = rawToken.left(trimmedLength);
            if (!isStartBoundary(textSegment, matchStart)
                || isMarkdownLinkDestinationContext(textSegment, matchStart)
                || !isDetectableWebLinkCandidate(candidate))
            {
                continue;
            }

            output += textSegment.mid(cursor, matchStart - cursor);
            output += ThinkingSpace::NoteBodyWebLinkSupport::canonicalStartTag(candidate);
            output += candidate;
            output += QStringLiteral("</weblink>");
            output += rawToken.mid(trimmedLength);
            cursor = matchEnd;
        }

        output += textSegment.mid(cursor);
        return output;
    }
} // namespace

namespace ThinkingSpace::NoteBodyWebLinkSupport
{
bool isWebLinkTagName(const QString& elementName)
{
    return normalizedTagName(elementName) == QStringLiteral("weblink");
}

bool containsDetectableWebLink(const QString& text)
{
    return autoWrapDetectedWebLinks(text) != normalizeLineEndings(text);
}

QString canonicalStartTag(const QString& href)
{
    return QStringLiteral("<weblink href=\"%1\">")
        .arg(escapeXmlAttributeValue(decodeXmlEntities(href).trimmed()));
}

QString canonicalStartTagFromRawToken(const QString& rawTagText)
{
    return canonicalStartTag(hrefAttributeFromRawToken(rawTagText));
}

QString activationUrlForHref(const QString& href)
{
    const QString decodedHref = decodeXmlEntities(href).trimmed();
    if (decodedHref.isEmpty())
    {
        return {};
    }

    const QUrl parsedUrl(decodedHref);
    if (parsedUrl.isValid() && !parsedUrl.scheme().trimmed().isEmpty())
    {
        return decodedHref;
    }

    if (decodedHref.startsWith(QStringLiteral("//")))
    {
        return QStringLiteral("https:%1").arg(decodedHref);
    }

    if (kWwwUrlPattern.match(decodedHref).hasMatch())
    {
        return QStringLiteral("https://%1").arg(decodedHref);
    }

    return decodedHref;
}

QString openingHtmlForHref(const QString& href)
{
    return QStringLiteral("<a href=\"%1\" style=\"color:#8CB4FF;text-decoration: underline;\">")
        .arg(escapeHtmlAttributeValue(activationUrlForHref(href)));
}

QString openingHtmlFromRawToken(const QString& rawTagText)
{
    return openingHtmlForHref(hrefAttributeFromRawToken(rawTagText));
}

QString autoWrapDetectedWebLinks(const QString& sourceText)
{
    const QString normalizedSourceText = normalizeLineEndings(sourceText);
    if (normalizedSourceText.isEmpty())
    {
        return {};
    }

    QString output;
    output.reserve(normalizedSourceText.size() + 64);

    qsizetype cursor = 0;
    int webLinkDepth = 0;
    QRegularExpressionMatchIterator iterator = kTagPattern.globalMatch(normalizedSourceText);
    while (iterator.hasNext())
    {
        const QRegularExpressionMatch match = iterator.next();
        const qsizetype tagStart = match.capturedStart(0);
        const qsizetype tagEnd = match.capturedEnd(0);
        if (tagStart < cursor || tagStart < 0 || tagEnd <= tagStart)
        {
            continue;
        }

        if (tagStart > cursor)
        {
            const QString literalSegment = normalizedSourceText.mid(cursor, tagStart - cursor);
            output += webLinkDepth > 0 ? literalSegment : wrapDetectedWebLinksInLiteralSegment(literalSegment);
        }

        const QString fullTagToken = match.captured(0);
        const QString rawTagName = match.captured(1);
        const bool closingTag = isClosingTagToken(fullTagToken);
        const bool selfClosingTag = isSelfClosingTagToken(fullTagToken);

        if (isWebLinkTagName(rawTagName))
        {
            if (closingTag)
            {
                webLinkDepth = std::max(0, webLinkDepth - 1);
                output += QStringLiteral("</weblink>");
            }
            else
            {
                output += canonicalStartTagFromRawToken(fullTagToken);
                if (selfClosingTag)
                {
                    output += QStringLiteral("</weblink>");
                }
                else
                {
                    ++webLinkDepth;
                }
            }
            cursor = tagEnd;
            continue;
        }

        output += fullTagToken;
        cursor = tagEnd;
    }

    if (cursor < normalizedSourceText.size())
    {
        const QString trailingSegment = normalizedSourceText.mid(cursor);
        output += webLinkDepth > 0 ? trailingSegment : wrapDetectedWebLinksInLiteralSegment(trailingSegment);
    }

    return output;
}
} // namespace ThinkingSpace::NoteBodyWebLinkSupport
