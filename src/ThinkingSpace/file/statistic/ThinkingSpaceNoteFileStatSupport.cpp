#include "ThinkingSpace/file/statistic/ThinkingSpaceNoteFileStatSupport.hpp"

#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderCreator.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderParser.hpp"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QTextStream>

#include <utility>

namespace
{
    QString readUtf8File(const QString& filePath, QString* errorMessage)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = file.errorString();
            }
            return {};
        }

        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        return QString::fromUtf8(file.readAll());
    }

    bool writeUtf8File(const QString& filePath, const QString& text, QString* errorMessage)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = file.errorString();
            }
            return false;
        }

        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << text;
        file.close();
        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        return true;
    }

    QString normalizedCountBodyText(QString text)
    {
        return ThinkingSpace::NoteBodyPersistence::normalizeBodyPlainText(std::move(text));
    }

    QStringList normalizedLines(const QString& bodySourceText)
    {
        const QString normalized = normalizedCountBodyText(bodySourceText);
        if (normalized.isEmpty())
        {
            return {};
        }

        return normalized.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    }

    QString normalizeBacklinkTarget(QString value)
    {
        value = value.trimmed();
        const int pipeIndex = value.indexOf(QLatin1Char('|'));
        if (pipeIndex >= 0)
        {
            value = value.left(pipeIndex).trimmed();
        }

        const int hashIndex = value.indexOf(QLatin1Char('#'));
        if (hashIndex >= 0)
        {
            value = value.left(hashIndex).trimmed();
        }

        return value;
    }

    QString hubRootPathForNoteDirectory(const QString& noteDirectoryPath)
    {
        QString currentPath = QDir::cleanPath(noteDirectoryPath.trimmed());
        if (currentPath.isEmpty())
        {
            return {};
        }

        QFileInfo currentInfo(currentPath);
        if (currentInfo.isFile())
        {
            currentPath = currentInfo.absolutePath();
        }

        QDir currentDirectory(currentPath);
        while (currentDirectory.exists())
        {
            if (currentDirectory.dirName().endsWith(QStringLiteral(".tshub"), Qt::CaseInsensitive))
            {
                return currentDirectory.absolutePath();
            }

            if (!currentDirectory.cdUp())
            {
                break;
            }
        }

        return {};
    }

    bool isHiddenHubPath(const QString& absolutePath, const QString& hubRootPath)
    {
        const QString relativePath = QDir(hubRootPath).relativeFilePath(absolutePath);
        const QStringList segments = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString& segment : segments)
        {
            if (segment.startsWith(QLatin1Char('.')))
            {
                return true;
            }
        }
        return false;
    }

    int countResourceTags(const QString& bodyDocumentText)
    {
        static const QRegularExpression resourcePattern(
            QStringLiteral(R"(<\s*resource\b[^>]*?/?>)"),
            QRegularExpression::CaseInsensitiveOption);

        int count = 0;
        QRegularExpressionMatchIterator iterator = resourcePattern.globalMatch(bodyDocumentText);
        while (iterator.hasNext())
        {
            iterator.next();
            ++count;
        }
        return count;
    }

    int countLetters(const QString& bodySourceText)
    {
        int count = 0;
        const QString normalized = normalizedCountBodyText(bodySourceText);
        for (const QChar ch : normalized)
        {
            if (!ch.isSpace())
            {
                ++count;
            }
        }
        return count;
    }

    int countWords(const QString& bodySourceText)
    {
        static const QRegularExpression wordPattern(QStringLiteral(R"([\p{L}\p{N}_]+)"));
        int count = 0;
        QRegularExpressionMatchIterator iterator = wordPattern.globalMatch(normalizedCountBodyText(bodySourceText));
        while (iterator.hasNext())
        {
            iterator.next();
            ++count;
        }
        return count;
    }

    int countSentences(const QString& bodySourceText)
    {
        const QString normalized = normalizedCountBodyText(bodySourceText).trimmed();
        if (normalized.isEmpty())
        {
            return 0;
        }

        int count = 0;
        const QStringList tokens = normalized.split(QRegularExpression(QStringLiteral(R"([.!?]+)")));
        for (const QString& token : tokens)
        {
            if (!token.trimmed().isEmpty())
            {
                ++count;
            }
        }
        return count;
    }

    int countParagraphs(const QString& bodySourceText)
    {
        int count = 0;
        for (const QString& line : normalizedLines(bodySourceText))
        {
            if (!line.trimmed().isEmpty())
            {
                ++count;
            }
        }
        return count;
    }

    int countSpaces(const QString& bodySourceText)
    {
        int count = 0;
        const QString normalized = normalizedCountBodyText(bodySourceText);
        for (const QChar ch : normalized)
        {
            if (ch == QLatin1Char(' '))
            {
                ++count;
            }
        }
        return count;
    }

    int countIndentCharacters(const QString& bodySourceText)
    {
        int count = 0;
        const QStringList lines = normalizedLines(bodySourceText);
        for (const QString& line : lines)
        {
            for (const QChar ch : line)
            {
                if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t'))
                {
                    ++count;
                    continue;
                }
                break;
            }
        }
        return count;
    }

    int countLines(const QString& bodySourceText)
    {
        return normalizedLines(bodySourceText).size();
    }

    QSet<QString> extractBacklinkTargetSet(const QString& bodySourceText, const QString& bodyDocumentText)
    {
        static const QRegularExpression wikiLinkPattern(
            QStringLiteral(R"(\[\[([^\]\r\n]+)\]\])"));
        static const QRegularExpression noteSchemePattern(
            QStringLiteral(R"(note://([A-Za-z0-9._-]+))"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression hrefPattern(
            QStringLiteral(R"(href\s*=\s*(?:\"note:([^\"]+)\"|'note:([^']+)'))"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression noteIdAttrPattern(
            QStringLiteral(R"(<\s*link\b[^>]*\bnoteId\s*=\s*(?:\"([^\"]+)\"|'([^']+)'))"),
            QRegularExpression::CaseInsensitiveOption);

        QSet<QString> targets;
        const auto appendMatches = [&targets](const QRegularExpression& pattern, const QString& text)
        {
            QRegularExpressionMatchIterator iterator = pattern.globalMatch(text);
            while (iterator.hasNext())
            {
                const QRegularExpressionMatch match = iterator.next();
                for (int captureIndex = 1; captureIndex <= match.lastCapturedIndex(); ++captureIndex)
                {
                    const QString normalizedTarget = normalizeBacklinkTarget(match.captured(captureIndex));
                    if (!normalizedTarget.isEmpty())
                    {
                        targets.insert(normalizedTarget);
                        break;
                    }
                }
            }
        };

        appendMatches(wikiLinkPattern, bodySourceText);
        appendMatches(noteSchemePattern, bodySourceText);
        appendMatches(hrefPattern, bodyDocumentText);
        appendMatches(noteIdAttrPattern, bodyDocumentText);
        return targets;
    }

    QString bodySourceTextFromDocument(const QString& bodyDocumentText)
    {
        const QString sourceText = ThinkingSpace::NoteBodyPersistence::sourceTextFromBodyDocument(bodyDocumentText);
        if (!sourceText.isEmpty())
        {
            return sourceText;
        }
        return ThinkingSpace::NoteBodyPersistence::plainTextFromBodyDocument(bodyDocumentText);
    }

    int backlinkByCountForNote(const QString& noteId, const QString& noteDirectoryPath, QString* errorMessage)
    {
        const QString normalizedNoteId = noteId.trimmed();
        if (normalizedNoteId.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                errorMessage->clear();
            }
            return 0;
        }

        const QString hubRootPath = hubRootPathForNoteDirectory(noteDirectoryPath);
        if (hubRootPath.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                errorMessage->clear();
            }
            return 0;
        }

        int incomingCount = 0;
        QDirIterator iterator(
            hubRootPath,
            QStringList{QStringLiteral("*.tsnbody")},
            QDir::Files,
            QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            const QString bodyFilePath = iterator.next();
            if (isHiddenHubPath(bodyFilePath, hubRootPath))
            {
                continue;
            }

            const QString ownerDirectoryPath = QFileInfo(bodyFilePath).absolutePath();
            if (QDir::cleanPath(ownerDirectoryPath) == QDir::cleanPath(noteDirectoryPath))
            {
                continue;
            }

            QString readError;
            const QString bodyDocumentText = readUtf8File(bodyFilePath, &readError);
            if (!readError.isEmpty())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = readError;
                }
                return 0;
            }

            if (extractBacklinkTargetSet(bodySourceTextFromDocument(bodyDocumentText), bodyDocumentText).contains(
                    normalizedNoteId))
            {
                ++incomingCount;
            }
        }

        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        return incomingCount;
    }

    void applyDerivedCounts(
        ThinkingSpaceNoteHeaderStore* headerStore,
        const QString& bodySourceText,
        const QString& bodyDocumentText,
        const int backlinkByCount)
    {
        if (headerStore == nullptr)
        {
            return;
        }

        headerStore->setTotalFolders(headerStore->folders().size());
        headerStore->setTotalTags(headerStore->tags().size());
        headerStore->setLetterCount(countLetters(bodySourceText));
        headerStore->setWordCount(countWords(bodySourceText));
        headerStore->setSentenceCount(countSentences(bodySourceText));
        headerStore->setParagraphCount(countParagraphs(bodySourceText));
        headerStore->setSpaceCount(countSpaces(bodySourceText));
        headerStore->setIndentCount(countIndentCharacters(bodySourceText));
        headerStore->setLineCount(countLines(bodySourceText));
        headerStore->setBacklinkToCount(extractBacklinkTargetSet(bodySourceText, bodyDocumentText).size());
        headerStore->setBacklinkByCount(backlinkByCount);
        headerStore->setIncludedResourceCount(countResourceTags(bodyDocumentText));
    }

    QString currentOpenTimestampUtc()
    {
        return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }

    bool refreshTrackedStatisticsInternal(
        const QString& noteId,
        const QString& noteDirectoryPath,
        const bool incrementOpenCount,
        QString* errorMessage)
    {
        const QString headerPath = ThinkingSpace::NoteBodyPersistence::resolveHeaderPath(QString(), noteDirectoryPath);
        if (headerPath.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to resolve .tsnhead path for tracked statistics.");
            }
            return false;
        }

        QString headerReadError;
        const QString headerText = readUtf8File(headerPath, &headerReadError);
        if (!headerReadError.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = headerReadError;
            }
            return false;
        }

        ThinkingSpaceNoteHeaderStore headerStore;
        ThinkingSpaceNoteHeaderParser parser;
        QString parseError;
        if (!parser.parse(headerText, &headerStore, &parseError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = parseError;
            }
            return false;
        }

        QString bodyDocumentText;
        const QString bodyPath = ThinkingSpace::NoteBodyPersistence::resolveBodyPath(noteDirectoryPath);
        if (!bodyPath.isEmpty() && QFileInfo(bodyPath).isFile())
        {
            QString bodyReadError;
            bodyDocumentText = readUtf8File(bodyPath, &bodyReadError);
            if (!bodyReadError.isEmpty())
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = bodyReadError;
                }
                return false;
            }
        }

        QString backlinkError;
        const int incomingBacklinkCount = backlinkByCountForNote(noteId, noteDirectoryPath, &backlinkError);
        if (!backlinkError.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = backlinkError;
            }
            return false;
        }

        applyDerivedCounts(&headerStore, bodySourceTextFromDocument(bodyDocumentText), bodyDocumentText, incomingBacklinkCount);
        if (incrementOpenCount)
        {
            headerStore.incrementOpenCount();
            headerStore.setLastOpenedAt(currentOpenTimestampUtc());
        }

        ThinkingSpaceNoteHeaderCreator creator(noteDirectoryPath, QString());
        QString writeError;
        if (!writeUtf8File(headerPath, creator.createHeaderText(headerStore), &writeError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = writeError;
            }
            return false;
        }

        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        return true;
    }

    bool incrementOpenCountForHeaderInternal(
        const QString& noteId,
        const QString& noteDirectoryPath,
        QString* errorMessage)
    {
        const QString normalizedNoteId = noteId.trimmed();
        if (normalizedNoteId.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("noteId must not be empty.");
            }
            return false;
        }

        const QString headerPath = ThinkingSpace::NoteBodyPersistence::resolveHeaderPath(QString(), noteDirectoryPath);
        if (headerPath.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("Failed to resolve .tsnhead path for open-count increment.");
            }
            return false;
        }

        QString headerReadError;
        const QString headerText = readUtf8File(headerPath, &headerReadError);
        if (!headerReadError.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = headerReadError;
            }
            return false;
        }

        ThinkingSpaceNoteHeaderStore headerStore;
        ThinkingSpaceNoteHeaderParser parser;
        QString parseError;
        if (!parser.parse(headerText, &headerStore, &parseError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = parseError;
            }
            return false;
        }

        if (headerStore.noteId().trimmed().isEmpty())
        {
            headerStore.setNoteId(normalizedNoteId);
        }
        headerStore.incrementOpenCount();
        headerStore.setLastOpenedAt(currentOpenTimestampUtc());

        ThinkingSpaceNoteHeaderCreator creator(noteDirectoryPath, QString());
        QString writeError;
        if (!writeUtf8File(headerPath, creator.createHeaderText(headerStore), &writeError))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = writeError;
            }
            return false;
        }

        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        return true;
    }

    QString noteDirectoryPathForNoteIdInHub(const QString& noteId, const QString& hubRootPath)
    {
        if (noteId.trimmed().isEmpty() || hubRootPath.trimmed().isEmpty())
        {
            return {};
        }

        QDirIterator iterator(
            hubRootPath,
            QStringList{QStringLiteral("*.tsnote")},
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            const QString noteDirectoryPath = iterator.next();
            if (isHiddenHubPath(noteDirectoryPath, hubRootPath))
            {
                continue;
            }

            const QString noteStem = QFileInfo(noteDirectoryPath).completeBaseName().trimmed();
            if (noteStem == noteId.trimmed())
            {
                return QDir::cleanPath(noteDirectoryPath);
            }
        }

        return {};
    }
} // namespace

QStringList ThinkingSpace::NoteFileStatSupport::extractBacklinkTargets(
    const QString& bodySourceText,
    const QString& bodyDocumentText)
{
    return extractBacklinkTargetSet(bodySourceText, bodyDocumentText).values();
}

void ThinkingSpace::NoteFileStatSupport::applyBodyDerivedStatistics(
    ThinkingSpaceNoteHeaderStore* headerStore,
    const QString& bodySourceText,
    const QString& bodyDocumentText)
{
    if (headerStore == nullptr)
    {
        return;
    }

    applyDerivedCounts(headerStore, bodySourceText, bodyDocumentText, headerStore->backlinkByCount());
}

bool ThinkingSpace::NoteFileStatSupport::applyTrackedStatistics(
    ThinkingSpaceNoteHeaderStore* headerStore,
    const QString& noteDirectoryPath,
    const QString& bodySourceText,
    const QString& bodyDocumentText,
    QString* errorMessage)
{
    if (headerStore == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("headerStore must not be null.");
        }
        return false;
    }

    QString backlinkError;
    const int incomingBacklinkCount = backlinkByCountForNote(
        headerStore->noteId(),
        noteDirectoryPath,
        &backlinkError);
    if (!backlinkError.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = backlinkError;
        }
        return false;
    }

    applyDerivedCounts(headerStore, bodySourceText, bodyDocumentText, incomingBacklinkCount);
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

bool ThinkingSpace::NoteFileStatSupport::incrementOpenCountForNoteHeader(
    const QString& noteId,
    const QString& noteDirectoryPath,
    QString* errorMessage)
{
    return incrementOpenCountForHeaderInternal(noteId, noteDirectoryPath, errorMessage);
}

bool ThinkingSpace::NoteFileStatSupport::refreshTrackedStatisticsForNote(
    const QString& noteId,
    const QString& noteDirectoryPath,
    const bool incrementOpenCount,
    QString* errorMessage)
{
    return refreshTrackedStatisticsInternal(noteId, noteDirectoryPath, incrementOpenCount, errorMessage);
}

bool ThinkingSpace::NoteFileStatSupport::refreshTrackedStatisticsForNoteId(
    const QString& noteId,
    const QString& referenceNoteDirectoryPath,
    QString* errorMessage)
{
    const QString hubRootPath = hubRootPathForNoteDirectory(referenceNoteDirectoryPath);
    if (hubRootPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        return true;
    }

    const QString targetNoteDirectoryPath = noteDirectoryPathForNoteIdInHub(noteId, hubRootPath);
    if (targetNoteDirectoryPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            errorMessage->clear();
        }
        return true;
    }

    return refreshTrackedStatisticsInternal(noteId, targetNoteDirectoryPath, false, errorMessage);
}
