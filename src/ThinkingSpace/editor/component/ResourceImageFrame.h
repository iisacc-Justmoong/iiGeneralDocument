#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QSize>
#include <QString>

namespace ThinkingSpace::EditorComponent
{
    struct ResourceFrameDescriptor final
    {
        QString sourceTag;
        QString resourcePath;
        QString resourceId;
        QString type;
        QString format;
        QString resolvedAssetPath;
        int editorViewportWidth = 0;
        int lockedFrameDisplayHeight = 0;
    };

    class ResourceFrame final
    {
    public:
        static QSize previewViewportSize();
        static QSize imageDisplaySize(const QSize& sourceSize);
        static QString sourceMarker(const QString& sourceTag);
        static QString renderHtml(const ResourceFrameDescriptor& descriptor);
    };
} // namespace ThinkingSpace::EditorComponent

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
