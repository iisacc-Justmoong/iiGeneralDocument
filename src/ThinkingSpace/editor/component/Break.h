#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>

namespace ThinkingSpace::EditorComponent
{
    class Break final
    {
    public:
        static QString sourceToken();
        static bool isSourceLine(const QString& sourceLine);
        static QString renderHtml();
    };
} // namespace ThinkingSpace::EditorComponent

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
