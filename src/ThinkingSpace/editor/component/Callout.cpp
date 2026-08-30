#include "ThinkingSpace/editor/component/Callout.h"

#include <QBuffer>
#include <QImage>
#include <QIODevice>
#include <QRegularExpression>
#include <QTextDocument>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kFigmaNodeWidth = 295;
    constexpr int kFramePaddingVertical = 4;
    constexpr int kFramePaddingRight = 4;
    constexpr int kFramePaddingLeft = 4;
    constexpr int kLeadingBarWidth = 3;
    constexpr int kLeadingBarHeight = 14;
    constexpr int kLeadingBarRadius = 3;
    constexpr int kContentGap = 12;
    constexpr int kFrameMinHeight = kFramePaddingVertical + kLeadingBarHeight + kFramePaddingVertical;
    constexpr auto kCalloutRenderVersion = "figma-280-7897-floated-frame-v5";

    QString normalizedContentHtml(const QString& contentHtml)
    {
        return contentHtml.trimmed().isEmpty() ? QStringLiteral("&nbsp;") : contentHtml;
    }

    QString plainTextForEditorDocumentText(const QString& editorDocumentText)
    {
        QTextDocument document;
        document.setHtml(editorDocumentText);
        return document.toPlainText();
    }

    int effectiveFrameWidth(const int editorViewportWidth)
    {
        return editorViewportWidth > 0 ? editorViewportWidth : kFigmaNodeWidth;
    }

    int estimatedContentHeight(const QString& contentHtml, const int editorViewportWidth)
    {
        QTextDocument document;
        document.setDocumentMargin(0);
        document.setTextWidth(qMax(
            1,
            effectiveFrameWidth(editorViewportWidth)
                - kFramePaddingLeft
                - kFramePaddingRight
                - kLeadingBarWidth
                - kContentGap));
        document.setHtml(QStringLiteral(
            "<span style=\"font-family:Pretendard;font-size:12px;font-weight:500;"
            "line-height:12px;white-space:normal;word-break:break-word;\">%1</span>")
            .arg(contentHtml));
        return qMax(kLeadingBarHeight, static_cast<int>(std::ceil(document.size().height())));
    }

    QString leadingBarPngBase64(const int contentHeight)
    {
        const int imageHeight = qMax(kLeadingBarHeight, contentHeight);
        QImage image(kLeadingBarWidth, imageHeight, QImage::Format_ARGB32);
        image.fill(Qt::transparent);

        const QRgb barColor = qRgba(217, 217, 217, 255);
        const int barBottom = qMin(imageHeight, contentHeight);
        for (int y = 0; y < barBottom; ++y)
        {
            for (int x = 0; x < kLeadingBarWidth; ++x)
            {
                image.setPixel(x, y, barColor);
            }
        }

        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return QString::fromLatin1(bytes.toBase64());
    }

    int clampedPosition(const int position, const int textSize)
    {
        return std::clamp(position, 0, textSize);
    }

    int decoratedCursorForVisibleCursor(
        const QString& editorDocumentText,
        const QString& sourceVisibleText,
        const int sourceVisibleCursorPosition)
    {
        const QString plainText = plainTextForEditorDocumentText(editorDocumentText);
        const int boundedVisibleCursor = clampedPosition(sourceVisibleCursorPosition, sourceVisibleText.size());

        int visibleCursor = 0;
        int editorCursor = 0;
        while (editorCursor < plainText.size())
        {
            if (visibleCursor >= boundedVisibleCursor)
            {
                return editorCursor;
            }
            if (visibleCursor >= sourceVisibleText.size())
            {
                ++editorCursor;
                continue;
            }

            if (sourceVisibleText.at(visibleCursor) == plainText.at(editorCursor))
            {
                ++visibleCursor;
            }
            ++editorCursor;
        }
        return plainText.size();
    }

    std::optional<ThinkingSpace::EditorComponent::CalloutSourceRange> rangeForVisibleCursor(
        const QString& bodySourceText,
        const int visibleCursorPosition,
        const ThinkingSpace::EditorComponent::Callout::SourceToVisibleCursor& sourceToVisibleCursor,
        const bool requireContentStart)
    {
        if (!sourceToVisibleCursor)
        {
            return std::nullopt;
        }

        const QVector<ThinkingSpace::EditorComponent::CalloutSourceRange> ranges =
            ThinkingSpace::EditorComponent::Callout::sourceRanges(bodySourceText);
        for (const ThinkingSpace::EditorComponent::CalloutSourceRange& range : ranges)
        {
            if (!range.isValid())
            {
                continue;
            }

            const int contentVisibleStart = sourceToVisibleCursor(range.contentStart);
            const int contentVisibleEnd = sourceToVisibleCursor(range.contentEnd);
            const bool matches = requireContentStart
                ? visibleCursorPosition == contentVisibleStart
                : visibleCursorPosition >= contentVisibleStart && visibleCursorPosition <= contentVisibleEnd;
            if (matches)
            {
                return range;
            }
        }
        return std::nullopt;
    }

    void expandEmptyCalloutRemovalToSourceLine(
        const QString& bodySourceText,
        int* removeStart,
        int* removeEnd)
    {
        if (removeStart == nullptr || removeEnd == nullptr)
        {
            return;
        }

        const bool hasPreviousLineBreak =
            *removeStart > 0 && bodySourceText.at(*removeStart - 1) == QLatin1Char('\n');
        const bool hasNextLineBreak =
            *removeEnd < bodySourceText.size() && bodySourceText.at(*removeEnd) == QLatin1Char('\n');
        if (hasPreviousLineBreak && hasNextLineBreak)
        {
            ++(*removeEnd);
            return;
        }
        if (hasPreviousLineBreak)
        {
            --(*removeStart);
            return;
        }
        if (hasNextLineBreak)
        {
            ++(*removeEnd);
        }
    }
} // namespace

namespace ThinkingSpace::EditorComponent
{
    bool CalloutSourceRange::isValid() const noexcept
    {
        return openingStart >= 0
            && openingEnd >= openingStart
            && contentStart == openingEnd
            && contentEnd >= contentStart
            && closingStart == contentEnd
            && closingEnd >= closingStart;
    }

    int Callout::designWidth()
    {
        return kFigmaNodeWidth;
    }

    QString Callout::sourceMarker(const QString& sourceText)
    {
        return QString::fromLatin1(sourceText.toUtf8().toHex());
    }

    QString Callout::renderHtml(const CalloutDescriptor& descriptor)
    {
        const QString contentHtml = normalizedContentHtml(descriptor.contentHtml);
        const int contentHeight = estimatedContentHeight(contentHtml, descriptor.editorViewportWidth);
        const int frameChromeHeight = qMax(kLeadingBarHeight, contentHeight);
        const QString leadingBarPng = leadingBarPngBase64(contentHeight);

        QString html;
        html.reserve(contentHtml.size() + 1200);
        html += QStringLiteral("<!--thinkingspace-callout-source:");
        html += sourceMarker(descriptor.sourceText);
        html += QStringLiteral("-->");
        html += QStringLiteral(
            "<div class=\"thinkingspace-callout\" data-figma-node-id=\"280:7897\" "
            "data-frame-design-width=\"%1\" data-callout-render=\"%2\" "
            "data-frame-width-mode=\"fill\" data-frame-height-mode=\"hug-contents\" "
            "data-frame-padding-top=\"%3\" data-frame-padding-right=\"%4\" "
            "data-frame-padding-bottom=\"%3\" data-frame-padding-left=\"%5\" "
            "data-callout-bar-width=\"%6\" data-callout-bar-design-height=\"%7\" "
            "data-callout-bar-height-mode=\"match-content\" "
            "data-callout-bar-radius=\"%8\" data-callout-content-gap=\"%9\" "
            "data-callout-frame-min-height=\"%10\" "
            "data-callout-frame-chrome-height=\"%11\" "
            "width=\"100%\" "
            "style=\"width:100%;max-width:100%;height:auto;min-height:0;"
            "box-sizing:border-box;background-color:#262728;"
            "padding:%3px %4px;"
            "font-family:Pretendard;font-size:12px;font-weight:500;line-height:12px;"
            "color:#FFFFFF;white-space:normal;word-break:break-word;\">")
            .arg(
                QString::number(kFigmaNodeWidth),
                QString::fromLatin1(kCalloutRenderVersion),
                QString::number(kFramePaddingVertical),
                QString::number(kFramePaddingRight),
                QString::number(kFramePaddingLeft),
                QString::number(kLeadingBarWidth),
                QString::number(kLeadingBarHeight),
                QString::number(kLeadingBarRadius),
                QString::number(kContentGap))
            .arg(QString::number(kFrameMinHeight))
            .arg(QString::number(frameChromeHeight));
        html += QStringLiteral(
            "<img class=\"thinkingspace-callout-leading-bar\" data-callout-frame-chrome=\"true\" "
            "src=\"data:image/png;base64,%1\" width=\"%2\" height=\"%3\" align=\"left\" "
            "style=\"border:0;float:left;margin-right:%4px;\"/>")
            .arg(
                leadingBarPng,
                QString::number(kLeadingBarWidth),
                QString::number(frameChromeHeight),
                QString::number(kContentGap));
        html += QStringLiteral(
            "<span data-callout-content=\"true\" "
            "style=\"background-color:#262728;font-family:Pretendard;font-size:12px;"
            "font-weight:500;line-height:12px;color:#FFFFFF;white-space:normal;"
            "word-break:break-word;\">"
            "<!--thinkingspace-callout-content-->");
        html += contentHtml;
        html += QStringLiteral("<!--/thinkingspace-callout-content--></span>");
        html += QStringLiteral(
            "</div>"
            "<!--/thinkingspace-callout-source-->");
        return html;
    }

    QVector<CalloutSourceRange> Callout::sourceRanges(const QString& bodySourceText)
    {
        struct OpeningCalloutTag final
        {
            int start = -1;
            int end = -1;
        };

        static const QRegularExpression calloutTagPattern(
            QStringLiteral(R"(<\s*(/?)\s*callout\b[^>]*>)"),
            QRegularExpression::CaseInsensitiveOption);

        QVector<OpeningCalloutTag> openingStack;
        QVector<CalloutSourceRange> ranges;
        QRegularExpressionMatchIterator matchIterator = calloutTagPattern.globalMatch(bodySourceText);
        while (matchIterator.hasNext())
        {
            const QRegularExpressionMatch match = matchIterator.next();
            const QString token = match.captured(0).trimmed();
            const bool closingTag = !match.captured(1).isEmpty();
            const bool selfClosingTag = token.endsWith(QStringLiteral("/>"));
            if (!closingTag && !selfClosingTag)
            {
                openingStack.push_back({
                    static_cast<int>(match.capturedStart(0)),
                    static_cast<int>(match.capturedEnd(0))
                });
                continue;
            }
            if (!closingTag || openingStack.isEmpty())
            {
                continue;
            }

            const OpeningCalloutTag opening = openingStack.takeLast();
            ranges.push_back({
                opening.start,
                opening.end,
                opening.end,
                static_cast<int>(match.capturedStart(0)),
                static_cast<int>(match.capturedStart(0)),
                static_cast<int>(match.capturedEnd(0))
            });
        }

        std::sort(
            ranges.begin(),
            ranges.end(),
            [](const CalloutSourceRange& left, const CalloutSourceRange& right)
            {
                return left.openingStart < right.openingStart;
            });
        return ranges;
    }

    int Callout::sourceVisibleCursorForDecoratedCursor(
        const QString& editorDocumentText,
        const QString& sourceVisibleText,
        const int decoratedCursorPosition)
    {
        const QString plainText = plainTextForEditorDocumentText(editorDocumentText);
        const int boundedCursorPosition = clampedPosition(decoratedCursorPosition, plainText.size());

        int visibleCursor = 0;
        int editorCursor = 0;
        while (editorCursor < boundedCursorPosition)
        {
            if (visibleCursor >= sourceVisibleText.size())
            {
                ++editorCursor;
                continue;
            }

            if (sourceVisibleText.at(visibleCursor) == plainText.at(editorCursor))
            {
                ++visibleCursor;
            }
            ++editorCursor;
        }
        return visibleCursor;
    }

    int Callout::decoratedContentStartForVisibleCursor(
        const QString& editorDocumentText,
        const QString& sourceVisibleText,
        const int sourceVisibleCursorPosition)
    {
        const QString plainText = plainTextForEditorDocumentText(editorDocumentText);
        const int boundedVisibleCursor = clampedPosition(sourceVisibleCursorPosition, sourceVisibleText.size());

        int visibleCursor = 0;
        int editorCursor = 0;
        while (editorCursor < plainText.size())
        {
            if (visibleCursor >= boundedVisibleCursor)
            {
                if (visibleCursor >= sourceVisibleText.size())
                {
                    return editorCursor;
                }

                const QChar nextSourceCharacter = sourceVisibleText.at(visibleCursor);
                if (plainText.at(editorCursor) != nextSourceCharacter
                    && nextSourceCharacter != QChar::ObjectReplacementCharacter)
                {
                    ++editorCursor;
                    continue;
                }
                return editorCursor;
            }

            if (visibleCursor >= sourceVisibleText.size())
            {
                ++editorCursor;
                continue;
            }

            const QChar currentSourceCharacter = sourceVisibleText.at(visibleCursor);
            if (plainText.at(editorCursor) != currentSourceCharacter
                && currentSourceCharacter != QChar::ObjectReplacementCharacter)
            {
                ++editorCursor;
                continue;
            }

            ++editorCursor;
            ++visibleCursor;
        }
        return plainText.size();
    }

    std::optional<CalloutBoundaryEdit> Callout::backspaceAtVisibleContentStart(
        const QString& bodySourceText,
        const int visibleCursorPosition,
        const SourceToVisibleCursor& sourceToVisibleCursor)
    {
        const std::optional<CalloutSourceRange> range =
            rangeForVisibleCursor(bodySourceText, visibleCursorPosition, sourceToVisibleCursor, true);
        if (!range.has_value())
        {
            return std::nullopt;
        }

        CalloutBoundaryEdit edit;
        edit.sourceCursorPosition = range->openingStart;
        if (range->contentStart == range->contentEnd)
        {
            int removeStart = range->openingStart;
            int removeEnd = range->closingEnd;
            expandEmptyCalloutRemovalToSourceLine(bodySourceText, &removeStart, &removeEnd);
            edit.bodySourceText = bodySourceText.left(removeStart) + bodySourceText.mid(removeEnd);
            edit.sourceCursorPosition = removeStart;
        }
        else
        {
            edit.bodySourceText =
                bodySourceText.left(range->openingStart)
                + bodySourceText.mid(range->contentStart, range->contentEnd - range->contentStart)
                + bodySourceText.mid(range->closingEnd);
        }
        edit.sourceCursorPosition = clampedPosition(edit.sourceCursorPosition, edit.bodySourceText.size());
        edit.changed = edit.bodySourceText != bodySourceText;
        return edit;
    }

    std::optional<CalloutBoundaryEdit> Callout::enterBeforeContentChrome(
        const QString& bodySourceText,
        const QString& editorDocumentText,
        const QString& sourceVisibleText,
        const int decoratedCursorPosition,
        const SourceToVisibleCursor& sourceToVisibleCursor)
    {
        if (!sourceToVisibleCursor)
        {
            return std::nullopt;
        }

        const QVector<CalloutSourceRange> ranges = sourceRanges(bodySourceText);
        for (const CalloutSourceRange& range : ranges)
        {
            if (!range.isValid())
            {
                continue;
            }

            const int contentVisibleCursor = sourceToVisibleCursor(range.contentStart);
            const int decoratedFrameStart = decoratedCursorForVisibleCursor(
                editorDocumentText,
                sourceVisibleText,
                contentVisibleCursor);
            const int decoratedContentStart = decoratedContentStartForVisibleCursor(
                editorDocumentText,
                sourceVisibleText,
                contentVisibleCursor);
            if (decoratedContentStart <= decoratedFrameStart
                || decoratedCursorPosition < decoratedFrameStart
                || decoratedCursorPosition >= decoratedContentStart)
            {
                continue;
            }

            CalloutBoundaryEdit edit;
            edit.bodySourceText = bodySourceText;
            edit.sourceCursorPosition = range.openingStart;
            edit.bodySourceText.insert(edit.sourceCursorPosition, QLatin1Char('\n'));
            ++edit.sourceCursorPosition;
            edit.changed = true;
            return edit;
        }
        return std::nullopt;
    }

    std::optional<CalloutBoundaryEdit> Callout::enterInsideVisibleCursor(
        const QString& bodySourceText,
        const int visibleCursorPosition,
        const SourceToVisibleCursor& sourceToVisibleCursor)
    {
        const std::optional<CalloutSourceRange> range =
            rangeForVisibleCursor(bodySourceText, visibleCursorPosition, sourceToVisibleCursor, false);
        if (!range.has_value())
        {
            return std::nullopt;
        }

        CalloutBoundaryEdit edit;
        edit.bodySourceText = bodySourceText;
        edit.sourceCursorPosition = range->closingEnd;
        if (edit.sourceCursorPosition < edit.bodySourceText.size()
            && edit.bodySourceText.at(edit.sourceCursorPosition) == QLatin1Char('\n'))
        {
            ++edit.sourceCursorPosition;
        }
        else
        {
            edit.bodySourceText.insert(edit.sourceCursorPosition, QLatin1Char('\n'));
            ++edit.sourceCursorPosition;
        }
        edit.changed = edit.bodySourceText != bodySourceText;
        return edit;
    }
} // namespace ThinkingSpace::EditorComponent
