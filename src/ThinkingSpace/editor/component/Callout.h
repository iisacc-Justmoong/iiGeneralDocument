#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <functional>
#include <optional>
#include <QString>
#include <QVector>

namespace ThinkingSpace::EditorComponent
{
    struct CalloutDescriptor final
    {
        QString sourceText;
        QString contentHtml;
        int editorViewportWidth = 0;
    };

    struct CalloutSourceRange final
    {
        int openingStart = -1;
        int openingEnd = -1;
        int contentStart = -1;
        int contentEnd = -1;
        int closingStart = -1;
        int closingEnd = -1;

        bool isValid() const noexcept;
    };

    struct CalloutBoundaryEdit final
    {
        QString bodySourceText;
        int sourceCursorPosition = 0;
        bool changed = false;
    };

    class Callout final
    {
    public:
        using SourceToVisibleCursor = std::function<int(int sourcePosition)>;

        static int designWidth();
        static QString sourceMarker(const QString& sourceText);
        static QString renderHtml(const CalloutDescriptor& descriptor);
        static QVector<CalloutSourceRange> sourceRanges(const QString& bodySourceText);
        static int sourceVisibleCursorForDecoratedCursor(
            const QString& editorDocumentText,
            const QString& sourceVisibleText,
            int decoratedCursorPosition);
        static int decoratedContentStartForVisibleCursor(
            const QString& editorDocumentText,
            const QString& sourceVisibleText,
            int sourceVisibleCursorPosition);
        static std::optional<CalloutBoundaryEdit> backspaceAtVisibleContentStart(
            const QString& bodySourceText,
            int visibleCursorPosition,
            const SourceToVisibleCursor& sourceToVisibleCursor);
        static std::optional<CalloutBoundaryEdit> enterBeforeContentChrome(
            const QString& bodySourceText,
            const QString& editorDocumentText,
            const QString& sourceVisibleText,
            int decoratedCursorPosition,
            const SourceToVisibleCursor& sourceToVisibleCursor);
        static std::optional<CalloutBoundaryEdit> enterInsideVisibleCursor(
            const QString& bodySourceText,
            int visibleCursorPosition,
            const SourceToVisibleCursor& sourceToVisibleCursor);
    };
} // namespace ThinkingSpace::EditorComponent

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
