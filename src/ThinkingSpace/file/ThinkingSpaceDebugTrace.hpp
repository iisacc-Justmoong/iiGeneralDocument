#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMessageLogContext>
#include <QObject>
#include <QString>
#include <QThread>
#include <QtGlobal>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <type_traits>

namespace ThinkingSpace::Debug
{
    inline bool parseBoolFlag(const QByteArray& rawValue, bool* outRecognized = nullptr)
    {
        const QByteArray normalized = rawValue.trimmed().toLower();
        const bool isTrue = normalized == "1"
            || normalized == "true"
            || normalized == "yes"
            || normalized == "on";
        const bool isFalse = normalized == "0"
            || normalized == "false"
            || normalized == "no"
            || normalized == "off";

        if (outRecognized != nullptr)
        {
            *outRecognized = isTrue || isFalse;
        }

        if (isTrue)
        {
            return true;
        }
        if (isFalse)
        {
            return false;
        }
        return true;
    }

    inline bool isEnabled()
    {
        static const bool enabled = []()
        {
            // Default-off to keep runtime overhead minimal in production.
            const QByteArray raw = qgetenv("THINKINGSPACE_DEBUG_MODE");
            if (raw.trimmed().isEmpty())
            {
                return false;
            }

            bool recognized = false;
            const bool parsed = parseBoolFlag(raw, &recognized);
            return recognized ? parsed : false;
        }();

        return enabled;
    }

    inline bool isEditorTraceEnabled()
    {
        static const bool enabled = []()
        {
            const QByteArray raw = qgetenv("THINKINGSPACE_EDITOR_TRACE");
            if (raw.trimmed().isEmpty())
            {
                return isEnabled();
            }

            bool recognized = false;
            const bool parsed = parseBoolFlag(raw, &recognized);
            return recognized ? parsed : isEnabled();
        }();

        return enabled;
    }

    inline bool isIiXmlTraceEnabled()
    {
        static const bool enabled = []()
        {
            const QByteArray raw = qgetenv("THINKINGSPACE_IIXML_TRACE_MODE");
            if (raw.trimmed().isEmpty())
            {
                return false;
            }

            bool recognized = false;
            const bool parsed = parseBoolFlag(raw, &recognized);
            return recognized ? parsed : false;
        }();

        return enabled;
    }

    inline bool isIiXmlTraceMessage(const QString& message)
    {
        return message.startsWith(QStringLiteral("iiXml::"));
    }

    inline bool shouldSuppressThirdPartyTraceMessage(
        const QtMsgType type,
        const QString& message,
        const bool iiXmlTraceEnabled)
    {
        return type == QtDebugMsg
            && !iiXmlTraceEnabled
            && isIiXmlTraceMessage(message);
    }

    inline QtMessageHandler& previousQtMessageHandlerStorage()
    {
        static QtMessageHandler previousHandler = nullptr;
        return previousHandler;
    }

    inline void writeDefaultFormattedQtMessage(
        const QtMsgType type,
        const QMessageLogContext& context,
        const QString& message)
    {
        const QByteArray formattedMessage = qFormatLogMessage(type, context, message).toLocal8Bit();
        std::fprintf(stderr, "%s\n", formattedMessage.constData());
        std::fflush(stderr);

        if (type == QtFatalMsg)
        {
            std::abort();
        }
    }

    inline void filteredThirdPartyTraceMessageHandler(
        const QtMsgType type,
        const QMessageLogContext& context,
        const QString& message)
    {
        if (shouldSuppressThirdPartyTraceMessage(type, message, isIiXmlTraceEnabled()))
        {
            return;
        }

        if (previousQtMessageHandlerStorage() != nullptr)
        {
            previousQtMessageHandlerStorage()(type, context, message);
            return;
        }

        writeDefaultFormattedQtMessage(type, context, message);
    }

    inline void installThirdPartyTraceMessageFilter()
    {
        static const bool installed = []()
        {
            previousQtMessageHandlerStorage() = qInstallMessageHandler(filteredThirdPartyTraceMessageHandler);
            return true;
        }();
        Q_UNUSED(installed);
    }

    inline QString normalizeSegment(const QString& value, const QString& fallback)
    {
        const QString trimmed = value.trimmed();
        return trimmed.isEmpty() ? fallback : trimmed;
    }

    inline std::atomic<quint64>& sequenceCounter()
    {
        static std::atomic<quint64> counter{0};
        return counter;
    }

    inline quint64 nextSequence()
    {
        return sequenceCounter().fetch_add(1, std::memory_order_relaxed) + 1;
    }

    inline QElapsedTimer& startupElapsedTimer()
    {
        static QElapsedTimer timer;
        static const bool initialized = []()
        {
            timer.start();
            return true;
        }();
        Q_UNUSED(initialized);
        return timer;
    }

    inline qint64 uptimeMilliseconds()
    {
        return startupElapsedTimer().elapsed();
    }

    inline QString shortSourceFileName(const char* fileName)
    {
        const QString filePath = QString::fromUtf8(fileName == nullptr ? "" : fileName);
        if (filePath.isEmpty())
        {
            return QStringLiteral("unknown");
        }

        const int slashIndex = filePath.lastIndexOf(u'/');
        const int backslashIndex = filePath.lastIndexOf(u'\\');
        const int separatorIndex = qMax(slashIndex, backslashIndex);
        if (separatorIndex < 0)
        {
            return filePath;
        }
        return filePath.mid(separatorIndex + 1);
    }

    inline QString sanitizeDetail(const QString& detail)
    {
        const QString normalized = detail.trimmed()
                                         .replace(u'\n', QStringLiteral("\\n"))
                                         .replace(u'\r', QStringLiteral("\\r"));
        if (normalized.isEmpty())
        {
            return {};
        }

        constexpr int kMaxDetailLength = 4096;
        if (normalized.size() <= kMaxDetailLength)
        {
            return normalized;
        }

        return QStringLiteral("%1...(truncated originalChars=%2)")
               .arg(normalized.left(kMaxDetailLength))
               .arg(normalized.size());
    }

    inline QString summarizeText(const QString& text, const int previewLength = 96)
    {
        const int safePreviewLength = std::max(0, previewLength);
        const QString preview = text.left(safePreviewLength)
            .normalized(QString::NormalizationForm_C)
            .replace(u'\n', QStringLiteral("\\n"))
            .replace(u'\r', QStringLiteral("\\r"));
        if (text.size() <= safePreviewLength)
        {
            return QStringLiteral("len=%1 preview=\"%2\"").arg(text.size()).arg(preview);
        }

        return QStringLiteral("len=%1 preview=\"%2\"...(truncated)").arg(text.size()).arg(preview);
    }

    inline QString ownerFromSignature(const QString& signature)
    {
        const QString normalized = signature.trimmed();
        if (normalized.isEmpty())
        {
            return {};
        }

        const int scopeIndex = normalized.indexOf(QStringLiteral("::"));
        if (scopeIndex < 0)
        {
            return {};
        }

        const int tokenStart = normalized.lastIndexOf(u' ', scopeIndex - 1);
        const int start = tokenStart < 0 ? 0 : tokenStart + 1;
        if (start >= scopeIndex)
        {
            return {};
        }
        return normalized.mid(start, scopeIndex - start).trimmed();
    }

    inline QString methodFromSignature(const QString& signature)
    {
        const QString normalized = signature.trimmed();
        if (normalized.isEmpty())
        {
            return {};
        }

        const int scopeIndex = normalized.lastIndexOf(QStringLiteral("::"));
        const int openParen = normalized.indexOf(u'(');
        const int end = openParen < 0 ? normalized.size() : openParen;
        if (end <= 0)
        {
            return {};
        }

        if (scopeIndex >= 0)
        {
            const int start = scopeIndex + 2;
            if (start >= end)
            {
                return {};
            }
            return normalized.mid(start, end - start).trimmed();
        }

        const int tokenStart = normalized.lastIndexOf(u' ', end - 1);
        const int start = tokenStart < 0 ? 0 : tokenStart + 1;
        if (start >= end)
        {
            return {};
        }
        return normalized.mid(start, end - start).trimmed();
    }

    inline void trace(
        const QString& scope,
        const QString& action,
        const QString& detail = QString(),
        const std::source_location& source = std::source_location::current())
    {
        if (!isEnabled())
        {
            return;
        }

        const quint64 sequence = nextSequence();
        const qint64 elapsedMs = uptimeMilliseconds();
        const qint64 processId = static_cast<qint64>(QCoreApplication::applicationPid());
        const quintptr threadId = reinterpret_cast<quintptr>(QThread::currentThreadId());
        const QString sourceFile = shortSourceFileName(source.file_name());
        const QString sourceFunction = QString::fromUtf8(
            source.function_name() == nullptr ? "" : source.function_name());
        const QString owner = ownerFromSignature(sourceFunction);
        const QString method = methodFromSignature(sourceFunction);

        const QString prefix = QStringLiteral(
                                   "[thinkingspace:debug][seq=%1][ms=%2][pid=%3][tid=0x%4][%5][%6][src=%7:%8][owner=%9][method=%10][fn=%11]")
                               .arg(sequence)
                               .arg(elapsedMs)
                               .arg(processId)
                               .arg(QString::number(threadId, 16))
                               .arg(normalizeSegment(scope, QStringLiteral("general")))
                               .arg(normalizeSegment(action, QStringLiteral("event")))
                               .arg(sourceFile)
                               .arg(source.line())
                               .arg(owner.isEmpty() ? QStringLiteral("global") : owner)
                               .arg(method.isEmpty() ? QStringLiteral("unknown") : method)
                               .arg(sourceFunction.isEmpty() ? QStringLiteral("unknown") : sourceFunction);

        const QString normalizedDetail = sanitizeDetail(detail);
        if (normalizedDetail.isEmpty())
        {
            qWarning().noquote() << prefix;
            return;
        }

        qWarning().noquote() << prefix << normalizedDetail;
    }

    inline void traceEditor(
        const QString& scope,
        const QString& action,
        const QString& detail = QString(),
        const std::source_location& source = std::source_location::current())
    {
        if (!isEditorTraceEnabled())
        {
            return;
        }

        trace(QStringLiteral("editor.%1").arg(normalizeSegment(scope, QStringLiteral("general"))), action, detail, source);
    }

    template <typename TObject>
    inline void traceSelf(
        const TObject* self,
        const QString& scope,
        const QString& action,
        const QString& detail = QString(),
        const std::source_location& source = std::source_location::current())
    {
        if (!isEnabled())
        {
            return;
        }

        QString selfDetail = QStringLiteral("selfPtr=0x%1")
            .arg(QString::number(reinterpret_cast<quintptr>(self), 16));

        const QString sourceFunction = QString::fromUtf8(
            source.function_name() == nullptr ? "" : source.function_name());
        const QString owner = ownerFromSignature(sourceFunction);
        if (!owner.isEmpty())
        {
            selfDetail += QStringLiteral(" selfClass=%1").arg(owner);
        }

        if constexpr (std::is_base_of_v<QObject, std::remove_cv_t<TObject>>)
        {
            if (self != nullptr)
            {
                const QObject* object = static_cast<const QObject*>(self);
                const QString metaClass = object->metaObject() == nullptr
                                              ? QStringLiteral("unknown")
                                              : QString::fromUtf8(object->metaObject()->className());
                selfDetail += QStringLiteral(" selfQObjectClass=%1").arg(metaClass);
                selfDetail += QStringLiteral(" selfObjectName=%1")
                    .arg(object->objectName().isEmpty() ? QStringLiteral("<empty>") : object->objectName());
            }
        }

        if (!detail.trimmed().isEmpty())
        {
            selfDetail += QStringLiteral(" | %1").arg(detail);
        }

        trace(scope, action, selfDetail, source);
    }

    template <typename TObject>
    inline void traceEditorSelf(
        const TObject* self,
        const QString& scope,
        const QString& action,
        const QString& detail = QString(),
        const std::source_location& source = std::source_location::current())
    {
        if (!isEditorTraceEnabled())
        {
            return;
        }

        traceSelf(
            self,
            QStringLiteral("editor.%1").arg(normalizeSegment(scope, QStringLiteral("general"))),
            action,
            detail,
            source);
    }
} // namespace ThinkingSpace::Debug

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
