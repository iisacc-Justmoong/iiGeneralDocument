#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>

namespace ThinkingSpace::NoteBodySemanticTagSupport
{
    QString canonicalInlineStyleTagName(const QString& elementName);
    QString canonicalRenderedTextBlockTagName(const QString& elementName);
    QString canonicalDocumentBlockTypeName(const QString& elementName);

    bool isHashtagTagName(const QString& elementName);
    bool isBreakDividerTagName(const QString& elementName);
    bool isResourceTagName(const QString& elementName);
    bool isWebLinkTagName(const QString& elementName);
    bool isStyleTagName(const QString& elementName);

    bool isSourceProjectionLineBreakTagName(const QString& elementName);
    bool isRenderedLineBreakTagName(const QString& elementName);

    bool isSourceProjectionTextBlockElement(const QString& elementName);
    bool isRenderedTextBlockElement(const QString& elementName);
    bool isTextualDocumentBlockTypeName(const QString& typeName);
    bool isExplicitDocumentBlockTypeName(const QString& typeName);

    bool isTransparentContainerTagName(const QString& elementName);
    bool isSourceSemanticPassThroughTagName(const QString& elementName);

    QString semanticTextOpeningHtml(const QString& elementName);
    QString semanticTextClosingHtml(const QString& elementName);
} // namespace ThinkingSpace::NoteBodySemanticTagSupport

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
