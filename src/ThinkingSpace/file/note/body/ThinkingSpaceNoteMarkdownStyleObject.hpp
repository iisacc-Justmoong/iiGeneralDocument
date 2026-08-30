#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

namespace ThinkingSpace
{
class ThinkingSpaceNoteMarkdownStyleObject final
{
public:
    enum class Role
    {
        UnorderedListMarker,
        OrderedListMarker,
        HeadingMarker,
        HeadingBody,
        BlockquoteMarker,
        BlockquoteBody,
        InlineCode,
        CodeFence,
        CodeBody,
        LinkLiteral,
        HorizontalRule,
    };

    struct PromotionMatch final
    {
        bool matched = false;
        QStringList promotedInlineTags;
    };

    static QString wrapHtmlSpan(Role role, const QString& innerHtml, int variant = 0);
    static PromotionMatch promotionMatchForCss(const QString& cssDeclaration);
    static QString canonicalUnorderedListSourceMarker();
};
} // namespace ThinkingSpace

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
