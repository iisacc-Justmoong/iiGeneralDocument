#include "ThinkingSpace/clipboard/FiletypeCapture.h"

#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcePackageSupport.hpp"

#include <QFileInfo>
#include <QHash>

namespace ThinkingSpace::Clipboard::FiletypeCapture
{
    QString normalizeMimeType(QString value)
    {
        value = value.trimmed().toCaseFolded();
        const int parameterIndex = value.indexOf(QLatin1Char(';'));
        if (parameterIndex >= 0)
        {
            value = value.left(parameterIndex).trimmed();
        }
        return value;
    }

    bool mimeTypeLooksLikeImagePayload(const QString& mimeType)
    {
        const QString normalized = normalizeMimeType(mimeType);
        return normalized.startsWith(QStringLiteral("image/"))
            || normalized == QStringLiteral("application/x-qt-image")
            || normalized == QStringLiteral("com.apple.tiff")
            || normalized == QStringLiteral("public.tiff")
            || normalized == QStringLiteral("public.png")
            || normalized == QStringLiteral("public.jpeg")
            || normalized.contains(QStringLiteral("image"))
            || normalized.contains(QStringLiteral("png"))
            || normalized.contains(QStringLiteral("jpeg"))
            || normalized.contains(QStringLiteral("jpg"))
            || normalized.contains(QStringLiteral("gif"))
            || normalized.contains(QStringLiteral("bmp"))
            || normalized.contains(QStringLiteral("webp"))
            || normalized.contains(QStringLiteral("tif"))
            || normalized.contains(QStringLiteral("tiff"))
            || normalized.contains(QStringLiteral("heic"))
            || normalized.contains(QStringLiteral("heif"));
    }

    QString formatFromMimeType(const QString& mimeType)
    {
        static const QHash<QString, QString> kFormatsByMimeType = {
            {QStringLiteral("image/png"), QStringLiteral(".png")},
            {QStringLiteral("image/jpeg"), QStringLiteral(".jpg")},
            {QStringLiteral("image/jpg"), QStringLiteral(".jpg")},
            {QStringLiteral("image/tiff"), QStringLiteral(".tiff")},
            {QStringLiteral("image/x-tiff"), QStringLiteral(".tiff")},
            {QStringLiteral("image/webp"), QStringLiteral(".webp")},
            {QStringLiteral("image/gif"), QStringLiteral(".gif")},
            {QStringLiteral("image/bmp"), QStringLiteral(".bmp")},
            {QStringLiteral("image/heic"), QStringLiteral(".heic")},
            {QStringLiteral("image/heif"), QStringLiteral(".heic")},
            {QStringLiteral("image/avif"), QStringLiteral(".avif")},
            {QStringLiteral("image/svg+xml"), QStringLiteral(".svg")},
            {QStringLiteral("application/x-qt-image"), QStringLiteral(".png")},
            {QStringLiteral("com.apple.tiff"), QStringLiteral(".png")},
            {QStringLiteral("public.png"), QStringLiteral(".png")},
            {QStringLiteral("public.jpeg"), QStringLiteral(".jpg")},
            {QStringLiteral("public.tiff"), QStringLiteral(".tiff")},
            {QStringLiteral("public.svg-image"), QStringLiteral(".svg")},
            {QStringLiteral("video/mp4"), QStringLiteral(".mp4")},
            {QStringLiteral("video/quicktime"), QStringLiteral(".mov")},
            {QStringLiteral("video/x-msvideo"), QStringLiteral(".avi")},
            {QStringLiteral("video/x-matroska"), QStringLiteral(".mkv")},
            {QStringLiteral("video/webm"), QStringLiteral(".webm")},
            {QStringLiteral("video/x-m4v"), QStringLiteral(".m4v")},
            {QStringLiteral("application/pdf"), QStringLiteral(".pdf")},
            {QStringLiteral("com.adobe.pdf"), QStringLiteral(".pdf")},
            {QStringLiteral("application/msword"), QStringLiteral(".doc")},
            {QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document"), QStringLiteral(".docx")},
            {QStringLiteral("text/plain"), QStringLiteral(".txt")},
            {QStringLiteral("public.plain-text"), QStringLiteral(".txt")},
            {QStringLiteral("text/markdown"), QStringLiteral(".md")},
            {QStringLiteral("text/html"), QStringLiteral(".html")},
            {QStringLiteral("public.html"), QStringLiteral(".html")},
            {QStringLiteral("application/rtf"), QStringLiteral(".rtf")},
            {QStringLiteral("text/rtf"), QStringLiteral(".rtf")},
            {QStringLiteral("application/vnd.ms-powerpoint"), QStringLiteral(".ppt")},
            {QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation"), QStringLiteral(".pptx")},
            {QStringLiteral("application/vnd.ms-excel"), QStringLiteral(".xls")},
            {QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"), QStringLiteral(".xlsx")},
            {QStringLiteral("text/csv"), QStringLiteral(".csv")},
            {QStringLiteral("model/gltf+json"), QStringLiteral(".gltf")},
            {QStringLiteral("model/gltf-binary"), QStringLiteral(".glb")},
            {QStringLiteral("model/obj"), QStringLiteral(".obj")},
            {QStringLiteral("model/stl"), QStringLiteral(".stl")},
            {QStringLiteral("model/vnd.usdz+zip"), QStringLiteral(".usdz")},
            {QStringLiteral("application/x-blender"), QStringLiteral(".blend")},
            {QStringLiteral("audio/mpeg"), QStringLiteral(".mp3")},
            {QStringLiteral("audio/aac"), QStringLiteral(".aac")},
            {QStringLiteral("audio/mp4"), QStringLiteral(".m4a")},
            {QStringLiteral("audio/x-m4a"), QStringLiteral(".m4a")},
            {QStringLiteral("audio/flac"), QStringLiteral(".flac")},
            {QStringLiteral("audio/wav"), QStringLiteral(".wav")},
            {QStringLiteral("audio/x-wav"), QStringLiteral(".wav")},
            {QStringLiteral("audio/ogg"), QStringLiteral(".ogg")},
            {QStringLiteral("audio/opus"), QStringLiteral(".opus")},
            {QStringLiteral("audio/aiff"), QStringLiteral(".aiff")},
            {QStringLiteral("audio/x-aiff"), QStringLiteral(".aiff")},
            {QStringLiteral("audio/x-caf"), QStringLiteral(".caf")},
            {QStringLiteral("audio/x-ms-wma"), QStringLiteral(".wma")},
            {QStringLiteral("audio/amr"), QStringLiteral(".amr")},
            {QStringLiteral("application/zip"), QStringLiteral(".zip")},
            {QStringLiteral("application/x-zip-compressed"), QStringLiteral(".zip")},
            {QStringLiteral("application/x-7z-compressed"), QStringLiteral(".7z")},
            {QStringLiteral("application/x-rar-compressed"), QStringLiteral(".rar")},
            {QStringLiteral("application/gzip"), QStringLiteral(".gz")},
            {QStringLiteral("application/x-tar"), QStringLiteral(".tar")},
            {QStringLiteral("application/x-bzip2"), QStringLiteral(".bz2")},
            {QStringLiteral("application/x-xz"), QStringLiteral(".xz")}
        };

        const QString normalized = normalizeMimeType(mimeType);
        if (kFormatsByMimeType.contains(normalized))
        {
            return kFormatsByMimeType.value(normalized);
        }

        if (normalized.startsWith(QStringLiteral("image/")))
        {
            return QStringLiteral(".png");
        }
        if (normalized == QStringLiteral("application/octet-stream"))
        {
            return QStringLiteral(".bin");
        }
        return {};
    }

    QString defaultResourceFileName(const QString& format)
    {
        const QString normalizedFormat = Resources::normalizeFormat(format).toCaseFolded();
        return QStringLiteral("clipboard-resource%1").arg(
            normalizedFormat.trimmed().isEmpty() ? QStringLiteral(".bin") : normalizedFormat);
    }

    QString normalizedFileNameOrDefault(const QString& fileName, const QString& format)
    {
        const QString trimmedFileName = QFileInfo(fileName.trimmed()).fileName().trimmed();
        if (!trimmedFileName.isEmpty())
        {
            return trimmedFileName;
        }
        return defaultResourceFileName(format);
    }

    QString normalizedFormatForFileType(const QString& fileName, const QString& mimeType)
    {
        QString format = Resources::formatFromAssetFilePath(fileName);
        if (format.trimmed().isEmpty())
        {
            format = formatFromMimeType(mimeType);
        }
        format = Resources::normalizeFormat(format);
        return format.trimmed().isEmpty() ? QStringLiteral(".bin") : format.toCaseFolded();
    }
} // namespace ThinkingSpace::Clipboard::FiletypeCapture
