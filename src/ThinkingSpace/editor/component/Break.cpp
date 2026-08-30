#include "ThinkingSpace/editor/component/Break.h"

#include <QRegularExpression>

namespace ThinkingSpace::EditorComponent
{
    QString Break::sourceToken()
    {
        return QStringLiteral("</break>");
    }

    bool Break::isSourceLine(const QString& sourceLine)
    {
        static const QRegularExpression kBreakLinePattern(
            QStringLiteral(R"(^(?:</\s*break\s*>|<\s*break\b[^>]*?/?>|<\s*hr\b[^>]*?/?>)$)"),
            QRegularExpression::CaseInsensitiveOption);
        return kBreakLinePattern.match(sourceLine.trimmed()).hasMatch();
    }

    QString Break::renderHtml()
    {
        return {};
    }
} // namespace ThinkingSpace::EditorComponent
