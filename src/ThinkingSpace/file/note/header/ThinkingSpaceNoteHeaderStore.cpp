#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderStore.hpp"

#include "ThinkingSpace/hierarchy/ThinkingSpaceFolderIdentity.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceBookmarkColorPalette.hpp"
#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"
#include "ThinkingSpace/file/note/folder/ThinkingSpaceNoteFolderSemantics.hpp"

#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace
{
    bool isTemplateToken(const QString& token)
    {
        const QString trimmed = token.trimmed();
        return (trimmed.startsWith(QStringLiteral("${")) && trimmed.endsWith(QLatin1Char('}')))
            || (trimmed.startsWith(QStringLiteral("%{")) && trimmed.endsWith(QLatin1Char('}')));
    }

    QString generatedNoteId()
    {
        return QStringLiteral("note-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    }

    bool isDatePlaceholderToken(const QString& value)
    {
        const QString normalized = value.trimmed().toCaseFolded();
        return normalized == QStringLiteral("yyyy-mm-dd-hh-mm-ss");
    }

    QString templatePayload(QString token)
    {
        token = token.trimmed();
        if (isTemplateToken(token) && token.size() >= 3)
        {
            token = token.mid(2, token.size() - 3).trimmed();
        }

        const int separatorIndex = token.indexOf(QLatin1Char(':'));
        if (separatorIndex >= 0)
        {
            token = token.left(separatorIndex).trimmed();
        }

        if ((token.startsWith(QLatin1Char('"')) && token.endsWith(QLatin1Char('"')) && token.size() >= 2)
            || (token.startsWith(QLatin1Char('\'')) && token.endsWith(QLatin1Char('\'')) && token.size() >= 2))
        {
            token = token.mid(1, token.size() - 2).trimmed();
        }

        token.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("-"));
        token.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9._/-]")));
        return token;
    }

    bool isGenericIdToken(const QString& token)
    {
        const QString normalized = token.trimmed().toCaseFolded();
        return normalized.isEmpty()
            || normalized == QStringLiteral("id")
            || normalized == QStringLiteral("note-id")
            || normalized == QStringLiteral("noteid");
    }

    QString sanitizeText(QString value)
    {
        return value.trimmed();
    }

    QStringList sanitizeStringList(QStringList values, const QString& fallbackPrefix)
    {
        QStringList sanitized;
        sanitized.reserve(values.size());
        Q_UNUSED(fallbackPrefix);

        for (QString& value : values)
        {
            value = value.trimmed();
            if (value.isEmpty())
            {
                continue;
            }

            if (isTemplateToken(value))
            {
                QString resolved = templatePayload(value);
                if (resolved.isEmpty())
                {
                    ThinkingSpace::Debug::trace(
                        QStringLiteral("note.header.store"),
                        QStringLiteral("sanitizeStringList.dropEmptyTemplateToken"));
                    continue;
                }
                sanitized.push_back(resolved);
                continue;
            }
            sanitized.push_back(value);
        }

        return sanitized;
    }

    struct SanitizedFolderBindings final
    {
        QStringList folders;
        QStringList folderUuids;
    };

    SanitizedFolderBindings sanitizeFolderBindings(QStringList values, QStringList folderUuids)
    {
        SanitizedFolderBindings sanitized;
        sanitized.folders.reserve(values.size());
        sanitized.folderUuids.reserve(values.size());
        QSet<QString> seenBindings;

        for (int index = 0; index < values.size(); ++index)
        {
            QString& value = values[index];
            value = value.trimmed();
            if (value.isEmpty())
            {
                continue;
            }

            if (isTemplateToken(value))
            {
                value = templatePayload(value);
                if (value.isEmpty())
                {
                    ThinkingSpace::Debug::trace(
                        QStringLiteral("note.header.store"),
                        QStringLiteral("sanitizeFolderList.dropEmptyTemplateToken"));
                    continue;
                }
            }

            if (ThinkingSpace::NoteFolders::usesReservedTodayFolderSegment(value))
            {
                ThinkingSpace::Debug::trace(
                    QStringLiteral("note.header.store"),
                    QStringLiteral("sanitizeFolderList.dropReservedTodayToken"),
                    QStringLiteral("value=%1").arg(value));
                continue;
            }

            const QString folderUuid = index < folderUuids.size()
                                           ? ThinkingSpace::FolderIdentity::normalizeFolderUuid(folderUuids.at(index))
                                           : QString();
            const QString bindingKey = !folderUuid.isEmpty()
                                           ? QStringLiteral("uuid:%1").arg(folderUuid)
                                           : QStringLiteral(
                                               "path:%1").arg(ThinkingSpace::NoteFolders::normalizeFolderPath(value)
                                                                  .toCaseFolded());
            if (bindingKey.endsWith(QLatin1Char(':')) || seenBindings.contains(bindingKey))
            {
                continue;
            }

            seenBindings.insert(bindingKey);
            sanitized.folders.push_back(value);
            sanitized.folderUuids.push_back(folderUuid);
        }

        return sanitized;
    }

    int sanitizeCountValue(const int value) noexcept
    {
        return std::max(value, 0);
    }
} // namespace

ThinkingSpaceNoteHeaderStore::ThinkingSpaceNoteHeaderStore() = default;

ThinkingSpaceNoteHeaderStore::~ThinkingSpaceNoteHeaderStore() = default;

void ThinkingSpaceNoteHeaderStore::clear()
{
    ThinkingSpace::Debug::traceSelf(this, QStringLiteral("note.header.store"), QStringLiteral("clear"));
    m_noteId.clear();
    m_createdAt.clear();
    m_author.clear();
    m_lastModifiedAt.clear();
    m_lastOpenedAt.clear();
    m_modifiedBy.clear();
    m_folders.clear();
    m_folderUuids.clear();
    m_project.clear();
    m_bookmarked = false;
    m_bookmarkColors.clear();
    m_tags.clear();
    m_totalFolders = 0;
    m_totalTags = 0;
    m_letterCount = 0;
    m_wordCount = 0;
    m_sentenceCount = 0;
    m_paragraphCount = 0;
    m_spaceCount = 0;
    m_indentCount = 0;
    m_lineCount = 0;
    m_openCount = 0;
    m_modifiedCount = 0;
    m_backlinkToCount = 0;
    m_backlinkByCount = 0;
    m_includedResourceCount = 0;
    m_progressEnums.clear();
    m_progress = -1;
    m_preset = false;
}

QString ThinkingSpaceNoteHeaderStore::noteId() const
{
    return m_noteId;
}

void ThinkingSpaceNoteHeaderStore::setNoteId(QString noteId)
{
    QString value = sanitizeText(std::move(noteId));
    if (value.isEmpty())
    {
        value = generatedNoteId();
    }
    else if (isTemplateToken(value))
    {
        const QString resolved = templatePayload(value);
        value = isGenericIdToken(resolved) ? generatedNoteId() : resolved;
    }

    m_noteId = value;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setNoteId"),
                              QStringLiteral("value=%1").arg(m_noteId));
}

QString ThinkingSpaceNoteHeaderStore::createdAt() const
{
    return m_createdAt;
}

void ThinkingSpaceNoteHeaderStore::setCreatedAt(QString createdAt)
{
    QString value = sanitizeText(std::move(createdAt));
    if (value.isEmpty() || isTemplateToken(value) || isDatePlaceholderToken(value))
    {
        value.clear();
    }

    m_createdAt = value;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setCreatedAt"),
                              QStringLiteral("value=%1").arg(m_createdAt));
}

QString ThinkingSpaceNoteHeaderStore::author() const
{
    return m_author;
}

void ThinkingSpaceNoteHeaderStore::setAuthor(QString author)
{
    QString value = sanitizeText(std::move(author));
    if (value.isEmpty())
    {
        value.clear();
    }
    else if (isTemplateToken(value))
    {
        const QString resolved = templatePayload(value);
        value = resolved;
    }

    m_author = value;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setAuthor"),
                              QStringLiteral("value=%1").arg(m_author));
}

QString ThinkingSpaceNoteHeaderStore::lastModifiedAt() const
{
    return m_lastModifiedAt;
}

void ThinkingSpaceNoteHeaderStore::setLastModifiedAt(QString lastModifiedAt)
{
    QString value = sanitizeText(std::move(lastModifiedAt));
    if (value.isEmpty() || isTemplateToken(value) || isDatePlaceholderToken(value))
    {
        value.clear();
    }

    m_lastModifiedAt = value;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setLastModifiedAt"),
                              QStringLiteral("value=%1").arg(m_lastModifiedAt));
}

QString ThinkingSpaceNoteHeaderStore::lastOpenedAt() const
{
    return m_lastOpenedAt;
}

void ThinkingSpaceNoteHeaderStore::setLastOpenedAt(QString lastOpenedAt)
{
    QString value = sanitizeText(std::move(lastOpenedAt));
    if (value.isEmpty() || isTemplateToken(value) || isDatePlaceholderToken(value))
    {
        value.clear();
    }

    m_lastOpenedAt = value;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setLastOpenedAt"),
                              QStringLiteral("value=%1").arg(m_lastOpenedAt));
}

QString ThinkingSpaceNoteHeaderStore::modifiedBy() const
{
    return m_modifiedBy;
}

void ThinkingSpaceNoteHeaderStore::setModifiedBy(QString modifiedBy)
{
    QString value = sanitizeText(std::move(modifiedBy));
    if (value.isEmpty())
    {
        value.clear();
    }
    else if (isTemplateToken(value))
    {
        const QString resolved = templatePayload(value);
        value = resolved;
    }

    m_modifiedBy = value;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setModifiedBy"),
                              QStringLiteral("value=%1").arg(m_modifiedBy));
}

QStringList ThinkingSpaceNoteHeaderStore::folders() const
{
    return m_folders;
}

void ThinkingSpaceNoteHeaderStore::setFolders(QStringList folders)
{
    setFolderBindings(std::move(folders), m_folderUuids);
}

QStringList ThinkingSpaceNoteHeaderStore::folderUuids() const
{
    return m_folderUuids;
}

void ThinkingSpaceNoteHeaderStore::setFolderUuids(QStringList folderUuids)
{
    setFolderBindings(m_folders, std::move(folderUuids));
}

void ThinkingSpaceNoteHeaderStore::setFolderBindings(QStringList folders, QStringList folderUuids)
{
    const int rawCount = folders.size();
    const SanitizedFolderBindings sanitized = sanitizeFolderBindings(
        std::move(folders),
        std::move(folderUuids));
    m_folders = sanitized.folders;
    m_folderUuids = sanitized.folderUuids;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setFolderBindings"),
                              QStringLiteral("rawCount=%1 sanitizedCount=%2 uuidCount=%3 values=[%4]")
                              .arg(rawCount)
                              .arg(m_folders.size())
                              .arg(m_folderUuids.size())
                              .arg(m_folders.join(QStringLiteral(", "))));
}

QString ThinkingSpaceNoteHeaderStore::project() const
{
    return m_project;
}

void ThinkingSpaceNoteHeaderStore::setProject(QString project)
{
    QString value = sanitizeText(std::move(project));
    if (value.isEmpty())
    {
        value.clear();
    }
    else if (isTemplateToken(value))
    {
        const QString resolved = templatePayload(value);
        value = resolved;
    }

    m_project = value;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setProject"),
                              QStringLiteral("value=%1").arg(m_project));
}

bool ThinkingSpaceNoteHeaderStore::isBookmarked() const noexcept
{
    return m_bookmarked;
}

void ThinkingSpaceNoteHeaderStore::setBookmarked(bool bookmarked) noexcept
{
    m_bookmarked = bookmarked;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setBookmarked"),
                              QStringLiteral("value=%1").arg(
                                  m_bookmarked ? QStringLiteral("true") : QStringLiteral("false")));
}

QStringList ThinkingSpaceNoteHeaderStore::bookmarkColors() const
{
    return m_bookmarkColors;
}

void ThinkingSpaceNoteHeaderStore::setBookmarkColors(QStringList colors)
{
    const int rawCount = colors.size();
    QStringList sanitized;
    sanitized.reserve(rawCount);

    for (const QString& color : colors)
    {
        const QString canonical = ThinkingSpace::Bookmarks::canonicalBookmarkColorToken(color);
        if (canonical.isEmpty() || sanitized.contains(canonical))
        {
            continue;
        }
        sanitized.push_back(canonical);
    }

    m_bookmarkColors = std::move(sanitized);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setBookmarkColors"),
                              QStringLiteral("rawCount=%1 sanitizedCount=%2 values=[%3]")
                              .arg(rawCount)
                              .arg(m_bookmarkColors.size())
                              .arg(m_bookmarkColors.join(QStringLiteral(", "))));
}

QStringList ThinkingSpaceNoteHeaderStore::tags() const
{
    return m_tags;
}

void ThinkingSpaceNoteHeaderStore::setTags(QStringList tags)
{
    const int rawCount = tags.size();
    m_tags = sanitizeStringList(std::move(tags), QStringLiteral("tag"));
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setTags"),
                              QStringLiteral("rawCount=%1 sanitizedCount=%2 values=[%3]")
                              .arg(rawCount)
                              .arg(m_tags.size())
                              .arg(m_tags.join(QStringLiteral(", "))));
}

int ThinkingSpaceNoteHeaderStore::totalFolders() const noexcept
{
    return m_totalFolders;
}

void ThinkingSpaceNoteHeaderStore::setTotalFolders(int totalFolders) noexcept
{
    m_totalFolders = sanitizeCountValue(totalFolders);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setTotalFolders"),
                              QStringLiteral("value=%1").arg(m_totalFolders));
}

int ThinkingSpaceNoteHeaderStore::totalTags() const noexcept
{
    return m_totalTags;
}

void ThinkingSpaceNoteHeaderStore::setTotalTags(int totalTags) noexcept
{
    m_totalTags = sanitizeCountValue(totalTags);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setTotalTags"),
                              QStringLiteral("value=%1").arg(m_totalTags));
}

int ThinkingSpaceNoteHeaderStore::letterCount() const noexcept
{
    return m_letterCount;
}

void ThinkingSpaceNoteHeaderStore::setLetterCount(int letterCount) noexcept
{
    m_letterCount = sanitizeCountValue(letterCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setLetterCount"),
                              QStringLiteral("value=%1").arg(m_letterCount));
}

int ThinkingSpaceNoteHeaderStore::wordCount() const noexcept
{
    return m_wordCount;
}

void ThinkingSpaceNoteHeaderStore::setWordCount(int wordCount) noexcept
{
    m_wordCount = sanitizeCountValue(wordCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setWordCount"),
                              QStringLiteral("value=%1").arg(m_wordCount));
}

int ThinkingSpaceNoteHeaderStore::sentenceCount() const noexcept
{
    return m_sentenceCount;
}

void ThinkingSpaceNoteHeaderStore::setSentenceCount(int sentenceCount) noexcept
{
    m_sentenceCount = sanitizeCountValue(sentenceCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setSentenceCount"),
                              QStringLiteral("value=%1").arg(m_sentenceCount));
}

int ThinkingSpaceNoteHeaderStore::paragraphCount() const noexcept
{
    return m_paragraphCount;
}

void ThinkingSpaceNoteHeaderStore::setParagraphCount(int paragraphCount) noexcept
{
    m_paragraphCount = sanitizeCountValue(paragraphCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setParagraphCount"),
                              QStringLiteral("value=%1").arg(m_paragraphCount));
}

int ThinkingSpaceNoteHeaderStore::spaceCount() const noexcept
{
    return m_spaceCount;
}

void ThinkingSpaceNoteHeaderStore::setSpaceCount(int spaceCount) noexcept
{
    m_spaceCount = sanitizeCountValue(spaceCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setSpaceCount"),
                              QStringLiteral("value=%1").arg(m_spaceCount));
}

int ThinkingSpaceNoteHeaderStore::indentCount() const noexcept
{
    return m_indentCount;
}

void ThinkingSpaceNoteHeaderStore::setIndentCount(int indentCount) noexcept
{
    m_indentCount = sanitizeCountValue(indentCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setIndentCount"),
                              QStringLiteral("value=%1").arg(m_indentCount));
}

int ThinkingSpaceNoteHeaderStore::lineCount() const noexcept
{
    return m_lineCount;
}

void ThinkingSpaceNoteHeaderStore::setLineCount(int lineCount) noexcept
{
    m_lineCount = sanitizeCountValue(lineCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setLineCount"),
                              QStringLiteral("value=%1").arg(m_lineCount));
}

int ThinkingSpaceNoteHeaderStore::openCount() const noexcept
{
    return m_openCount;
}

void ThinkingSpaceNoteHeaderStore::setOpenCount(int openCount) noexcept
{
    m_openCount = sanitizeCountValue(openCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setOpenCount"),
                              QStringLiteral("value=%1").arg(m_openCount));
}

void ThinkingSpaceNoteHeaderStore::incrementOpenCount() noexcept
{
    setOpenCount(m_openCount + 1);
}

int ThinkingSpaceNoteHeaderStore::modifiedCount() const noexcept
{
    return m_modifiedCount;
}

void ThinkingSpaceNoteHeaderStore::setModifiedCount(int modifiedCount) noexcept
{
    m_modifiedCount = sanitizeCountValue(modifiedCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setModifiedCount"),
                              QStringLiteral("value=%1").arg(m_modifiedCount));
}

void ThinkingSpaceNoteHeaderStore::incrementModifiedCount() noexcept
{
    setModifiedCount(m_modifiedCount + 1);
}

int ThinkingSpaceNoteHeaderStore::backlinkToCount() const noexcept
{
    return m_backlinkToCount;
}

void ThinkingSpaceNoteHeaderStore::setBacklinkToCount(int backlinkToCount) noexcept
{
    m_backlinkToCount = sanitizeCountValue(backlinkToCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setBacklinkToCount"),
                              QStringLiteral("value=%1").arg(m_backlinkToCount));
}

int ThinkingSpaceNoteHeaderStore::backlinkByCount() const noexcept
{
    return m_backlinkByCount;
}

void ThinkingSpaceNoteHeaderStore::setBacklinkByCount(int backlinkByCount) noexcept
{
    m_backlinkByCount = sanitizeCountValue(backlinkByCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setBacklinkByCount"),
                              QStringLiteral("value=%1").arg(m_backlinkByCount));
}

int ThinkingSpaceNoteHeaderStore::includedResourceCount() const noexcept
{
    return m_includedResourceCount;
}

void ThinkingSpaceNoteHeaderStore::setIncludedResourceCount(int includedResourceCount) noexcept
{
    m_includedResourceCount = sanitizeCountValue(includedResourceCount);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setIncludedResourceCount"),
                              QStringLiteral("value=%1").arg(m_includedResourceCount));
}

QStringList ThinkingSpaceNoteHeaderStore::progressEnums() const
{
    return m_progressEnums;
}

void ThinkingSpaceNoteHeaderStore::setProgressEnums(QStringList progressEnums)
{
    const int rawCount = progressEnums.size();
    m_progressEnums = sanitizeStringList(std::move(progressEnums), QStringLiteral("progress"));
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setProgressEnums"),
                              QStringLiteral("rawCount=%1 sanitizedCount=%2 values=[%3]")
                              .arg(rawCount)
                              .arg(m_progressEnums.size())
                              .arg(m_progressEnums.join(QStringLiteral(", "))));
}

int ThinkingSpaceNoteHeaderStore::progress() const noexcept
{
    return m_progress;
}

void ThinkingSpaceNoteHeaderStore::setProgress(int progress) noexcept
{
    m_progress = std::max(progress, -1);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setProgress"),
                              QStringLiteral("value=%1").arg(m_progress));
}

bool ThinkingSpaceNoteHeaderStore::isPreset() const noexcept
{
    return m_preset;
}

void ThinkingSpaceNoteHeaderStore::setPreset(bool preset) noexcept
{
    m_preset = preset;
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.header.store"),
                              QStringLiteral("setPreset"),
                              QStringLiteral("value=%1").arg(
                                  m_preset ? QStringLiteral("true") : QStringLiteral("false")));
}
