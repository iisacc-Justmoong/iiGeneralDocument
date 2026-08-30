#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteMarkdownStyleObject.hpp"

#include <algorithm>
#include <QHash>
#include <QStringList>

namespace
{
    using CssPropertyMap = QHash<QString, QString>;

    QString normalizedCssValue(const QString& propertyName, QString value)
    {
        value = value.trimmed().toCaseFolded();
        if (propertyName == QStringLiteral("font-family"))
        {
            value.remove(QLatin1Char('\"'));
            value.remove(QLatin1Char('\''));
        }
        value = value.simplified();
        return value;
    }

    CssPropertyMap parseCssDeclaration(const QString& cssDeclaration)
    {
        CssPropertyMap properties;
        const QStringList declarations = cssDeclaration.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString& declaration : declarations)
        {
            const int separatorIndex = declaration.indexOf(QLatin1Char(':'));
            if (separatorIndex <= 0)
            {
                continue;
            }

            const QString propertyName = declaration.left(separatorIndex).trimmed().toCaseFolded();
            if (propertyName.isEmpty())
            {
                continue;
            }

            properties.insert(
                propertyName,
                normalizedCssValue(propertyName, declaration.mid(separatorIndex + 1)));
        }

        return properties;
    }

    bool cssPropertyEquals(const CssPropertyMap& properties, const QString& propertyName, const QString& expectedValue)
    {
        const auto it = properties.constFind(propertyName);
        if (it == properties.constEnd())
        {
            return false;
        }

        return it.value() == normalizedCssValue(propertyName, expectedValue);
    }

    bool cssPropertyContainsToken(const CssPropertyMap& properties, const QString& propertyName, const QString& token)
    {
        const auto it = properties.constFind(propertyName);
        if (it == properties.constEnd())
        {
            return false;
        }

        return it.value().contains(token.toCaseFolded());
    }

    bool cssHeadingBodyMatches(const CssPropertyMap& properties)
    {
        if (!cssPropertyEquals(properties, QStringLiteral("font-weight"), QStringLiteral("800"))
            || !cssPropertyEquals(properties, QStringLiteral("color"), QStringLiteral("#F3F5F8")))
        {
            return false;
        }

        const auto it = properties.constFind(QStringLiteral("font-size"));
        if (it == properties.constEnd())
        {
            return false;
        }

        static const QStringList allowedSizes = {
            QStringLiteral("22px"),
            QStringLiteral("20px"),
            QStringLiteral("18px"),
            QStringLiteral("16px"),
            QStringLiteral("14px"),
            QStringLiteral("13px"),
        };
        return allowedSizes.contains(it.value());
    }

    bool cssMatchesRole(
        const CssPropertyMap& properties,
        const ThinkingSpace::ThinkingSpaceNoteMarkdownStyleObject::Role role)
    {
        using Role = ThinkingSpace::ThinkingSpaceNoteMarkdownStyleObject::Role;
        switch (role)
        {
        case Role::UnorderedListMarker:
        case Role::OrderedListMarker:
            return cssPropertyEquals(properties, QStringLiteral("font-weight"), QStringLiteral("700"))
                && cssPropertyEquals(properties, QStringLiteral("color"), QStringLiteral("#C9CDD4"));
        case Role::HeadingMarker:
            return cssPropertyEquals(properties, QStringLiteral("font-weight"), QStringLiteral("600"))
                && cssPropertyEquals(properties, QStringLiteral("color"), QStringLiteral("#8F96A3"));
        case Role::HeadingBody:
            return cssHeadingBodyMatches(properties);
        case Role::BlockquoteMarker:
            return cssPropertyEquals(properties, QStringLiteral("color"), QStringLiteral("#8F96A3"))
                && !cssPropertyContainsToken(properties, QStringLiteral("font-style"), QStringLiteral("italic"));
        case Role::BlockquoteBody:
            return cssPropertyEquals(properties, QStringLiteral("color"), QStringLiteral("#C9CDD4"))
                && cssPropertyContainsToken(properties, QStringLiteral("font-style"), QStringLiteral("italic"));
        case Role::InlineCode:
        case Role::CodeBody:
            return cssPropertyEquals(properties, QStringLiteral("font-family"), QStringLiteral("Menlo"))
                && cssPropertyEquals(properties, QStringLiteral("background-color"), QStringLiteral("#1A1D22"))
                && cssPropertyEquals(properties, QStringLiteral("color"), QStringLiteral("#D7DADF"));
        case Role::CodeFence:
            return cssPropertyEquals(properties, QStringLiteral("font-family"), QStringLiteral("Menlo"))
                && cssPropertyEquals(properties, QStringLiteral("background-color"), QStringLiteral("#1A1D22"))
                && cssPropertyEquals(properties, QStringLiteral("color"), QStringLiteral("#8F96A3"));
        case Role::LinkLiteral:
            return cssPropertyEquals(properties, QStringLiteral("color"), QStringLiteral("#8CB4FF"))
                && cssPropertyContainsToken(properties, QStringLiteral("text-decoration"), QStringLiteral("underline"));
        case Role::HorizontalRule:
            return cssPropertyEquals(properties, QStringLiteral("color"), QStringLiteral("#66727D"))
                && cssPropertyEquals(properties, QStringLiteral("letter-spacing"), QStringLiteral("2px"));
        }

        return false;
    }

    QString cssDeclarationForRole(
        const ThinkingSpace::ThinkingSpaceNoteMarkdownStyleObject::Role role,
        const int variant)
    {
        using Role = ThinkingSpace::ThinkingSpaceNoteMarkdownStyleObject::Role;
        switch (role)
        {
        case Role::UnorderedListMarker:
        case Role::OrderedListMarker:
            return QStringLiteral("font-weight:700;color:#C9CDD4;");
        case Role::HeadingMarker:
            return QStringLiteral("color:#8F96A3;font-weight:600;");
        case Role::HeadingBody:
        {
            static const int headingPixelSizes[] = {22, 20, 18, 16, 14, 13};
            const int boundedVariant = std::clamp(variant, 1, 6) - 1;
            return QStringLiteral("font-weight:800;font-size:%1px;color:#F3F5F8;")
                .arg(QString::number(headingPixelSizes[boundedVariant]));
        }
        case Role::BlockquoteMarker:
            return QStringLiteral("color:#8F96A3;");
        case Role::BlockquoteBody:
            return QStringLiteral("color:#C9CDD4;font-style:italic;");
        case Role::InlineCode:
        case Role::CodeBody:
            return QStringLiteral("font-family:'Menlo';background-color:#1A1D22;color:#D7DADF;");
        case Role::CodeFence:
            return QStringLiteral("font-family:'Menlo';background-color:#1A1D22;color:#8F96A3;");
        case Role::LinkLiteral:
            return QStringLiteral("color:#8CB4FF;text-decoration: underline;");
        case Role::HorizontalRule:
            return QStringLiteral("color:#66727D;letter-spacing:2px;");
        }

        return {};
    }

    QStringList promotedInlineTagsForRole(const ThinkingSpace::ThinkingSpaceNoteMarkdownStyleObject::Role role)
    {
        using Role = ThinkingSpace::ThinkingSpaceNoteMarkdownStyleObject::Role;
        switch (role)
        {
        case Role::HeadingBody:
            return {QStringLiteral("bold")};
        case Role::BlockquoteBody:
            return {QStringLiteral("italic")};
        case Role::InlineCode:
        case Role::CodeBody:
            return {QStringLiteral("highlight")};
        case Role::LinkLiteral:
            return {QStringLiteral("underline")};
        case Role::UnorderedListMarker:
        case Role::OrderedListMarker:
        case Role::HeadingMarker:
        case Role::BlockquoteMarker:
        case Role::CodeFence:
        case Role::HorizontalRule:
            return {};
        }

        return {};
    }
} // namespace

namespace ThinkingSpace
{
QString ThinkingSpaceNoteMarkdownStyleObject::wrapHtmlSpan(const Role role, const QString& innerHtml, const int variant)
{
    const QString cssDeclaration = cssDeclarationForRole(role, variant);
    if (cssDeclaration.isEmpty())
    {
        return innerHtml;
    }

    return QStringLiteral("<span style=\"%1\">%2</span>").arg(cssDeclaration, innerHtml);
}

ThinkingSpaceNoteMarkdownStyleObject::PromotionMatch
ThinkingSpaceNoteMarkdownStyleObject::promotionMatchForCss(const QString& cssDeclaration)
{
    const CssPropertyMap properties = parseCssDeclaration(cssDeclaration);
    if (properties.isEmpty())
    {
        return {};
    }

    static const Role orderedRoles[] = {
        Role::UnorderedListMarker,
        Role::OrderedListMarker,
        Role::HeadingMarker,
        Role::HeadingBody,
        Role::BlockquoteMarker,
        Role::BlockquoteBody,
        Role::InlineCode,
        Role::CodeFence,
        Role::CodeBody,
        Role::LinkLiteral,
        Role::HorizontalRule,
    };

    for (const Role role : orderedRoles)
    {
        if (!cssMatchesRole(properties, role))
        {
            continue;
        }

        return {
            true,
            promotedInlineTagsForRole(role)
        };
    }

    return {};
}

QString ThinkingSpaceNoteMarkdownStyleObject::canonicalUnorderedListSourceMarker()
{
    return QStringLiteral("-");
}
} // namespace ThinkingSpace
