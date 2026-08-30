#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodySemanticTagSupport.hpp"

#include <algorithm>
#include <array>

namespace
{
    QString normalizedTagName(const QString& elementName)
    {
        return elementName.trimmed().toCaseFolded();
    }

    QString headingOpenHtml(const int headingLevel)
    {
        static constexpr std::array<int, 6> headingPixelSizes = {22, 20, 18, 16, 14, 13};
        const int boundedLevel = std::clamp(headingLevel, 1, static_cast<int>(headingPixelSizes.size()));
        return QStringLiteral("<span style=\"font-weight:800;font-size:%1px;color:#F3F5F8;\">")
            .arg(QString::number(headingPixelSizes.at(static_cast<std::size_t>(boundedLevel - 1))));
    }

    int semanticHeadingLevel(const QString& elementName)
    {
        const QString normalizedName = normalizedTagName(elementName);
        if (normalizedName == QStringLiteral("header")
            || normalizedName == QStringLiteral("title")
            || normalizedName == QStringLiteral("h1"))
        {
            return 1;
        }
        if (normalizedName == QStringLiteral("subheader")
            || normalizedName == QStringLiteral("subtitle")
            || normalizedName == QStringLiteral("h2"))
        {
            return 2;
        }
        if (normalizedName == QStringLiteral("eventtitle") || normalizedName == QStringLiteral("h3"))
        {
            return 3;
        }
        if (normalizedName == QStringLiteral("h4"))
        {
            return 4;
        }
        if (normalizedName == QStringLiteral("h5"))
        {
            return 5;
        }
        if (normalizedName == QStringLiteral("h6"))
        {
            return 6;
        }
        return 0;
    }

    bool isLegacySemanticTextBlockTag(const QString& elementName)
    {
        const QString normalizedName = normalizedTagName(elementName);
        return normalizedName == QStringLiteral("title")
            || normalizedName == QStringLiteral("header")
            || normalizedName == QStringLiteral("subheader")
            || normalizedName == QStringLiteral("subtitle")
            || normalizedName == QStringLiteral("eventtitle")
            || normalizedName == QStringLiteral("eventdescription");
    }
} // namespace

namespace ThinkingSpace::NoteBodySemanticTagSupport
{
QString canonicalInlineStyleTagName(const QString& elementName)
{
    const QString normalizedName = normalizedTagName(elementName);
    if (normalizedName == QStringLiteral("bold")
        || normalizedName == QStringLiteral("b")
        || normalizedName == QStringLiteral("strong"))
    {
        return QStringLiteral("bold");
    }
    if (normalizedName == QStringLiteral("italic")
        || normalizedName == QStringLiteral("i")
        || normalizedName == QStringLiteral("em"))
    {
        return QStringLiteral("italic");
    }
    if (normalizedName == QStringLiteral("underline")
        || normalizedName == QStringLiteral("u"))
    {
        return QStringLiteral("underline");
    }
    if (normalizedName == QStringLiteral("strikethrough")
        || normalizedName == QStringLiteral("strike")
        || normalizedName == QStringLiteral("s")
        || normalizedName == QStringLiteral("del"))
    {
        return QStringLiteral("strikethrough");
    }
    if (normalizedName == QStringLiteral("highlight")
        || normalizedName == QStringLiteral("mark"))
    {
        return QStringLiteral("highlight");
    }
    return {};
}

QString canonicalRenderedTextBlockTagName(const QString& elementName)
{
    const QString normalizedName = normalizedTagName(elementName);
    if (normalizedName.isEmpty())
    {
        return {};
    }

    if (normalizedName == QStringLiteral("p")
        || normalizedName == QStringLiteral("paragraph")
        || normalizedName == QStringLiteral("div")
        || normalizedName == QStringLiteral("li")
        || normalizedName == QStringLiteral("blockquote")
        || normalizedName == QStringLiteral("pre")
        || normalizedName == QStringLiteral("h1")
        || normalizedName == QStringLiteral("h2")
        || normalizedName == QStringLiteral("h3")
        || normalizedName == QStringLiteral("h4")
        || normalizedName == QStringLiteral("h5")
        || normalizedName == QStringLiteral("h6")
        || isLegacySemanticTextBlockTag(normalizedName))
    {
        return normalizedName;
    }
    return {};
}

QString canonicalDocumentBlockTypeName(const QString& elementName)
{
    const QString normalizedName = normalizedTagName(elementName);
    if (normalizedName == QStringLiteral("resource"))
    {
        return QStringLiteral("resource");
    }
    if (normalizedName == QStringLiteral("break")
        || normalizedName == QStringLiteral("hr"))
    {
        return QStringLiteral("break");
    }
    if (normalizedName == QStringLiteral("callout"))
    {
        return QStringLiteral("paragraph");
    }
    return canonicalRenderedTextBlockTagName(elementName);
}

bool isHashtagTagName(const QString& elementName)
{
    return normalizedTagName(elementName) == QStringLiteral("tag");
}

bool isBreakDividerTagName(const QString& elementName)
{
    const QString normalizedName = normalizedTagName(elementName);
    return normalizedName == QStringLiteral("break")
        || normalizedName == QStringLiteral("hr");
}

bool isResourceTagName(const QString& elementName)
{
    return normalizedTagName(elementName) == QStringLiteral("resource");
}

bool isWebLinkTagName(const QString& elementName)
{
    return normalizedTagName(elementName) == QStringLiteral("weblink");
}

bool isStyleTagName(const QString& elementName)
{
    return normalizedTagName(elementName) == QStringLiteral("style");
}

bool isSourceProjectionLineBreakTagName(const QString& elementName)
{
    return normalizedTagName(elementName) == QStringLiteral("br");
}

bool isRenderedLineBreakTagName(const QString& elementName)
{
    const QString normalizedName = normalizedTagName(elementName);
    return normalizedName == QStringLiteral("br")
        || normalizedName == QStringLiteral("next");
}

bool isSourceProjectionTextBlockElement(const QString& elementName)
{
    const QString normalizedName = normalizedTagName(elementName);
    return normalizedName == QStringLiteral("p")
        || normalizedName == QStringLiteral("paragraph")
        || normalizedName == QStringLiteral("div")
        || normalizedName == QStringLiteral("li")
        || normalizedName == QStringLiteral("blockquote")
        || normalizedName == QStringLiteral("pre")
        || normalizedName == QStringLiteral("h1")
        || normalizedName == QStringLiteral("h2")
        || normalizedName == QStringLiteral("h3")
        || normalizedName == QStringLiteral("h4")
        || normalizedName == QStringLiteral("h5")
        || normalizedName == QStringLiteral("h6");
}

bool isRenderedTextBlockElement(const QString& elementName)
{
    return !canonicalRenderedTextBlockTagName(elementName).isEmpty();
}

bool isTextualDocumentBlockTypeName(const QString& typeName)
{
    const QString normalizedName = normalizedTagName(typeName);
    return normalizedName == QStringLiteral("text")
        || !canonicalRenderedTextBlockTagName(normalizedName).isEmpty();
}

bool isExplicitDocumentBlockTypeName(const QString& typeName)
{
    return !canonicalDocumentBlockTypeName(typeName).isEmpty();
}

bool isTransparentContainerTagName(const QString& elementName)
{
    const QString normalizedName = normalizedTagName(elementName);
    return normalizedName == QStringLiteral("event")
        || normalizedName == QStringLiteral("callout");
}

bool isSourceSemanticPassThroughTagName(const QString& elementName)
{
    return normalizedTagName(elementName) == QStringLiteral("next")
        || isStyleTagName(elementName)
        || isTransparentContainerTagName(elementName)
        || isLegacySemanticTextBlockTag(elementName);
}

QString semanticTextOpeningHtml(const QString& elementName)
{
    const int headingLevel = semanticHeadingLevel(elementName);
    if (headingLevel > 0)
    {
        return headingOpenHtml(headingLevel);
    }
    return {};
}

QString semanticTextClosingHtml(const QString& elementName)
{
    if (!semanticTextOpeningHtml(elementName).isEmpty())
    {
        return QStringLiteral("</span>");
    }
    return {};
}
} // namespace ThinkingSpace::NoteBodySemanticTagSupport
