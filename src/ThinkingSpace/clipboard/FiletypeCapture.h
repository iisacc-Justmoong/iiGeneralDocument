#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>

namespace ThinkingSpace::Clipboard::FiletypeCapture
{
    QString normalizeMimeType(QString value);
    bool mimeTypeLooksLikeImagePayload(const QString& mimeType);
    QString formatFromMimeType(const QString& mimeType);
    QString defaultResourceFileName(const QString& format);
    QString normalizedFileNameOrDefault(const QString& fileName, const QString& format);
    QString normalizedFormatForFileType(const QString& fileName, const QString& mimeType);
} // namespace ThinkingSpace::Clipboard::FiletypeCapture

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
