#include "ThinkingSpace/editor/component/ResourceImageFrame.h"

#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcePackageSupport.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QStandardPaths>
#include <QUrl>

namespace
{
    constexpr int kFigmaFrameWidth = 480;
    constexpr auto kFrameRenderVersion = "figma-292-50-atomic-image-frame-v2";

    QString htmlAttribute(QString value)
    {
        return value.toHtmlEscaped();
    }

    bool isImageDescriptor(const ThinkingSpace::EditorComponent::ResourceFrameDescriptor& descriptor)
    {
        const QString normalizedType = ThinkingSpace::Resources::normalizedType(descriptor.type);
        const QString normalizedFormat = ThinkingSpace::Resources::normalizeFormat(descriptor.format).toCaseFolded();
        return normalizedType == QStringLiteral("image")
            || ThinkingSpace::Resources::inferTypeFromFormat(normalizedFormat) == QStringLiteral("image");
    }

    QSize imageSourceSize(const ThinkingSpace::EditorComponent::ResourceFrameDescriptor& descriptor)
    {
        if (!QFileInfo(descriptor.resolvedAssetPath).isFile())
        {
            return {};
        }

        QImageReader imageReader(descriptor.resolvedAssetPath);
        imageReader.setAutoTransform(true);
        const QSize imageSize = imageReader.size();
        return imageSize.isValid() && !imageSize.isEmpty() ? imageSize : QSize();
    }

    QSize fallbackMediaDisplaySize()
    {
        return QSize(640, 360);
    }

    QSize mediaDisplaySizeForSource(const QSize& sourceSize)
    {
        const QSize displaySize = ThinkingSpace::EditorComponent::ResourceFrame::imageDisplaySize(sourceSize);
        return displaySize.isValid() && !displaySize.isEmpty()
            ? displaySize
            : fallbackMediaDisplaySize();
    }

    int frameRenderWidthForViewport(const int editorViewportWidth)
    {
        return editorViewportWidth > 0 ? qMax(1, editorViewportWidth) : kFigmaFrameWidth;
    }

    QSize mediaDisplaySizeForLockedHeight(const QSize& sourceSize, const int lockedFrameDisplayHeight)
    {
        const QSize displaySize = mediaDisplaySizeForSource(sourceSize);
        if (!displaySize.isValid() || displaySize.isEmpty())
        {
            return QSize(qMax(1, lockedFrameDisplayHeight), qMax(1, lockedFrameDisplayHeight));
        }

        const qreal mediaScale = static_cast<qreal>(qMax(1, lockedFrameDisplayHeight))
            / static_cast<qreal>(displaySize.height());
        return QSize(
            qMax(1, qRound(static_cast<qreal>(displaySize.width()) * mediaScale)),
            qMax(1, lockedFrameDisplayHeight));
    }

    QSize mediaDisplaySizeForFrame(
        const QSize& sourceSize,
        const int frameRenderWidth,
        const int lockedFrameDisplayHeight)
    {
        if (lockedFrameDisplayHeight > 0)
        {
            return mediaDisplaySizeForLockedHeight(sourceSize, lockedFrameDisplayHeight);
        }

        const QSize displaySize = mediaDisplaySizeForSource(sourceSize);
        if (!displaySize.isValid() || displaySize.isEmpty())
        {
            return QSize(qMax(1, frameRenderWidth), 1);
        }

        if (displaySize.width() <= frameRenderWidth)
        {
            return displaySize;
        }

        const qreal mediaScale = static_cast<qreal>(frameRenderWidth) / static_cast<qreal>(displaySize.width());
        return QSize(
            frameRenderWidth,
            qMax(1, qRound(static_cast<qreal>(displaySize.height()) * mediaScale)));
    }

    QSize mediaRasterSizeForSource(
        const QSize& sourceSize,
        const int frameRenderWidth,
        const int lockedFrameDisplayHeight)
    {
        const QSize displaySize = mediaDisplaySizeForFrame(sourceSize, frameRenderWidth, lockedFrameDisplayHeight);
        if (lockedFrameDisplayHeight > 0)
        {
            return QSize(qMax(1, frameRenderWidth), qMax(1, lockedFrameDisplayHeight));
        }

        if (!displaySize.isValid() || displaySize.isEmpty())
        {
            return QSize(qMax(1, frameRenderWidth), 1);
        }

        return QSize(qMax(1, frameRenderWidth), displaySize.height());
    }

    QPoint mediaDisplayTopLeft(const QSize& displaySize, const int frameRenderWidth)
    {
        return QPoint((frameRenderWidth - displaySize.width()) / 2, 0);
    }

    int frameDisplayHeightForSource(
        const QSize& sourceSize,
        const int frameRenderWidth,
        const int lockedFrameDisplayHeight)
    {
        return mediaRasterSizeForSource(sourceSize, frameRenderWidth, lockedFrameDisplayHeight).height();
    }

    QString frameMetricAttributes(
        const QSize& sourceSize,
        const int frameRenderWidth,
        const int lockedFrameDisplayHeight)
    {
        QString attributes = QStringLiteral(
                                 " data-frame-design-width=\"%1\""
                                 " data-frame-render-width=\"%2\"")
            .arg(
                QString::number(kFigmaFrameWidth),
                QString::number(frameRenderWidth));

        if (!sourceSize.isValid() || sourceSize.isEmpty())
        {
            return attributes;
        }

        const QSize displaySize = mediaDisplaySizeForFrame(
            sourceSize,
            frameRenderWidth,
            lockedFrameDisplayHeight);
        const QPoint displayTopLeft = mediaDisplayTopLeft(displaySize, frameRenderWidth);
        attributes += QStringLiteral(
                          " data-source-width=\"%1\" data-source-height=\"%2\""
                          " data-display-width=\"%3\" data-display-height=\"%4\""
                          " data-display-left=\"%5\" data-display-top=\"%6\""
                          " data-frame-display-height=\"%7\"")
            .arg(
                QString::number(sourceSize.width()),
                QString::number(sourceSize.height()),
                QString::number(displaySize.width()),
                QString::number(displaySize.height()),
                QString::number(displayTopLeft.x()),
                QString::number(displayTopLeft.y()),
                QString::number(frameDisplayHeightForSource(
                    sourceSize,
                    frameRenderWidth,
                    lockedFrameDisplayHeight)));
        return attributes;
    }

    QImage readSourceImage(const ThinkingSpace::EditorComponent::ResourceFrameDescriptor& descriptor)
    {
        if (!QFileInfo(descriptor.resolvedAssetPath).isFile())
        {
            return {};
        }

        QImageReader imageReader(descriptor.resolvedAssetPath);
        imageReader.setAutoTransform(true);
        QImage image = imageReader.read();
        if (image.isNull())
        {
            return {};
        }
        if (image.format() != QImage::Format_ARGB32_Premultiplied)
        {
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        return image;
    }

    QImage renderMediaPreviewImage(
        const ThinkingSpace::EditorComponent::ResourceFrameDescriptor& descriptor,
        const QSize& sourceSize)
    {
        const int frameRenderWidth = frameRenderWidthForViewport(descriptor.editorViewportWidth);
        const QSize displaySize = mediaDisplaySizeForFrame(
            sourceSize,
            frameRenderWidth,
            descriptor.lockedFrameDisplayHeight);
        const QSize mediaSize = mediaRasterSizeForSource(
            sourceSize,
            frameRenderWidth,
            descriptor.lockedFrameDisplayHeight);
        if (!mediaSize.isValid() || mediaSize.isEmpty())
        {
            return {};
        }

        QImage media(mediaSize, QImage::Format_ARGB32_Premultiplied);
        media.fill(QColor(QStringLiteral("#1E1F20")));

        QPainter painter(&media);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (isImageDescriptor(descriptor))
        {
            const QImage sourceImage = readSourceImage(descriptor);
            if (!sourceImage.isNull())
            {
                const QImage scaledImage = sourceImage.scaled(
                    displaySize,
                    Qt::KeepAspectRatio,
                    Qt::SmoothTransformation);
                const QPoint displayTopLeft = mediaDisplayTopLeft(displaySize, frameRenderWidth);
                const QPoint mediaTopLeft(
                    displayTopLeft.x() + ((displaySize.width() - scaledImage.width()) / 2),
                    displayTopLeft.y() + ((displaySize.height() - scaledImage.height()) / 2));
                painter.drawImage(mediaTopLeft, scaledImage);
            }
        }

        return media;
    }

    QString dataUriForFrameImage(const QImage& frameImage)
    {
        if (frameImage.isNull())
        {
            return {};
        }

        QByteArray encodedImage;
        QBuffer buffer(&encodedImage);
        if (!buffer.open(QIODevice::WriteOnly) || !frameImage.save(&buffer, "PNG"))
        {
            return {};
        }
        return QStringLiteral("data:image/png;base64,%1")
            .arg(QString::fromLatin1(encodedImage.toBase64()));
    }

    QString framePreviewCachePath(
        const ThinkingSpace::EditorComponent::ResourceFrameDescriptor& descriptor,
        const QSize& sourceSize)
    {
        QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (cacheRoot.trimmed().isEmpty())
        {
            cacheRoot = QDir(QDir::tempPath()).filePath(QStringLiteral("Thinking Space"));
        }

        QByteArray key;
        key += kFrameRenderVersion;
        key += '\0';
        key += QByteArray::number(frameRenderWidthForViewport(descriptor.editorViewportWidth));
        key += '\0';
        key += QByteArray::number(qMax(0, descriptor.lockedFrameDisplayHeight));
        key += '\0';
        key += QByteArray::number(kFigmaFrameWidth);
        key += '\0';
        key += descriptor.sourceTag.toUtf8();
        key += '\0';
        key += descriptor.resourcePath.toUtf8();
        key += '\0';
        key += descriptor.resolvedAssetPath.toUtf8();
        key += '\0';
        key += QByteArray::number(sourceSize.width());
        key += 'x';
        key += QByteArray::number(sourceSize.height());

        const QFileInfo assetInfo(descriptor.resolvedAssetPath);
        if (assetInfo.isFile())
        {
            key += '\0';
            key += QByteArray::number(assetInfo.size());
            key += '\0';
            key += QByteArray::number(assetInfo.lastModified().toMSecsSinceEpoch());
        }

        const QString cacheFileName =
            QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().left(32))
            + QStringLiteral(".png");
        return QDir(cacheRoot).filePath(QStringLiteral("resource-frames/%1").arg(cacheFileName));
    }

    QString mediaPreviewImageUrl(
        const ThinkingSpace::EditorComponent::ResourceFrameDescriptor& descriptor,
        const QSize& sourceSize)
    {
        const QImage mediaImage = renderMediaPreviewImage(descriptor, sourceSize);
        if (mediaImage.isNull())
        {
            return {};
        }

        const QString cachePath = framePreviewCachePath(descriptor, sourceSize);
        const QFileInfo cacheInfo(cachePath);
        QDir cacheDir(cacheInfo.absolutePath());
        if ((cacheDir.exists() || QDir().mkpath(cacheInfo.absolutePath()))
            && (cacheInfo.isFile() || mediaImage.save(cachePath, "PNG")))
        {
            return QUrl::fromLocalFile(cachePath).toString();
        }

        return dataUriForFrameImage(mediaImage);
    }

} // namespace

namespace ThinkingSpace::EditorComponent
{
    QSize ResourceFrame::previewViewportSize()
    {
        return {};
    }

    QSize ResourceFrame::imageDisplaySize(const QSize& sourceSize)
    {
        if (!sourceSize.isValid() || sourceSize.isEmpty())
        {
            return {};
        }

        const int sourceWidth = qMax(1, sourceSize.width());
        const int sourceHeight = qMax(1, sourceSize.height());
        return QSize(sourceWidth, sourceHeight > sourceWidth ? sourceWidth : sourceHeight);
    }

    QString ResourceFrame::sourceMarker(const QString& sourceTag)
    {
        return QString::fromLatin1(sourceTag.toUtf8().toHex());
    }

    QString ResourceFrame::renderHtml(const ResourceFrameDescriptor& descriptor)
    {
        const QSize sourceImageSize =
            isImageDescriptor(descriptor) ? imageSourceSize(descriptor) : QSize();
        const int frameRenderWidth = frameRenderWidthForViewport(descriptor.editorViewportWidth);
        const int frameDisplayHeight = frameDisplayHeightForSource(
            sourceImageSize,
            frameRenderWidth,
            qMax(0, descriptor.lockedFrameDisplayHeight));
        const QString previewImageUrl = mediaPreviewImageUrl(descriptor, sourceImageSize);
        const QString metricAttributes = frameMetricAttributes(
            sourceImageSize,
            frameRenderWidth,
            qMax(0, descriptor.lockedFrameDisplayHeight));

        QString html;
        html.reserve(2600);
        html += QStringLiteral("<!--thinkingspace-resource-source:");
        html += sourceMarker(descriptor.sourceTag);
        html += QStringLiteral("-->");
        html += QStringLiteral(
            "<img src=\"%2\" alt=\"\" class=\"thinkingspace-resource-frame thinkingspace-resource-media\" "
            "data-figma-node-id=\"292:50\" "
            "data-resource-preview=\"image-only-frame\" data-media-preview=\"media-raster\" "
            "data-max-width-height-ratio=\"1:1\"%1 "
            "data-media-alignment=\"center\" "
            "width=\"%3\" height=\"%4\" "
            "style=\"display:block;width:%3px;height:%4px;max-width:100%;max-height:100%;"
            "margin-top:0px;margin-bottom:0px;margin-left:0px;margin-right:0px;"
            "vertical-align:top;object-fit:contain;\" />")
            .arg(
                metricAttributes,
                htmlAttribute(previewImageUrl),
                QString::number(frameRenderWidth),
                QString::number(frameDisplayHeight));
        html += QStringLiteral("<!--/thinkingspace-resource-source-->");
        return html;
    }

} // namespace ThinkingSpace::EditorComponent
