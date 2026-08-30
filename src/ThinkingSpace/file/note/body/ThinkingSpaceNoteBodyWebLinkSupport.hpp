#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>

namespace ThinkingSpace::NoteBodyWebLinkSupport
{
    bool isWebLinkTagName(const QString& elementName);
    bool containsDetectableWebLink(const QString& text);
    QString canonicalStartTag(const QString& href);
    QString canonicalStartTagFromRawToken(const QString& rawTagText);
    QString openingHtmlForHref(const QString& href);
    QString openingHtmlFromRawToken(const QString& rawTagText);
    QString activationUrlForHref(const QString& href);
    QString autoWrapDetectedWebLinks(const QString& sourceText);
} // namespace ThinkingSpace::NoteBodyWebLinkSupport

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
