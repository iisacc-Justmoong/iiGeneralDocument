#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

class ThinkingSpaceNoteHeaderStore
{
public:
    ThinkingSpaceNoteHeaderStore();
    ~ThinkingSpaceNoteHeaderStore();

    void clear();

    QString noteId() const;
    void setNoteId(QString noteId);

    QString createdAt() const;
    void setCreatedAt(QString createdAt);

    QString author() const;
    void setAuthor(QString author);

    QString lastModifiedAt() const;
    void setLastModifiedAt(QString lastModifiedAt);

    QString lastOpenedAt() const;
    void setLastOpenedAt(QString lastOpenedAt);

    QString modifiedBy() const;
    void setModifiedBy(QString modifiedBy);

    QStringList folders() const;
    void setFolders(QStringList folders);
    QStringList folderUuids() const;
    void setFolderUuids(QStringList folderUuids);
    void setFolderBindings(QStringList folders, QStringList folderUuids);

    QString project() const;
    void setProject(QString project);

    bool isBookmarked() const noexcept;
    void setBookmarked(bool bookmarked) noexcept;
    QStringList bookmarkColors() const;
    void setBookmarkColors(QStringList colors);

    QStringList tags() const;
    void setTags(QStringList tags);

    int totalFolders() const noexcept;
    void setTotalFolders(int totalFolders) noexcept;

    int totalTags() const noexcept;
    void setTotalTags(int totalTags) noexcept;

    int letterCount() const noexcept;
    void setLetterCount(int letterCount) noexcept;

    int wordCount() const noexcept;
    void setWordCount(int wordCount) noexcept;

    int sentenceCount() const noexcept;
    void setSentenceCount(int sentenceCount) noexcept;

    int paragraphCount() const noexcept;
    void setParagraphCount(int paragraphCount) noexcept;

    int spaceCount() const noexcept;
    void setSpaceCount(int spaceCount) noexcept;

    int indentCount() const noexcept;
    void setIndentCount(int indentCount) noexcept;

    int lineCount() const noexcept;
    void setLineCount(int lineCount) noexcept;

    int openCount() const noexcept;
    void setOpenCount(int openCount) noexcept;
    void incrementOpenCount() noexcept;

    int modifiedCount() const noexcept;
    void setModifiedCount(int modifiedCount) noexcept;
    void incrementModifiedCount() noexcept;

    int backlinkToCount() const noexcept;
    void setBacklinkToCount(int backlinkToCount) noexcept;

    int backlinkByCount() const noexcept;
    void setBacklinkByCount(int backlinkByCount) noexcept;

    int includedResourceCount() const noexcept;
    void setIncludedResourceCount(int includedResourceCount) noexcept;

    QStringList progressEnums() const;
    void setProgressEnums(QStringList progressEnums);

    int progress() const noexcept;
    void setProgress(int progress) noexcept;

    bool isPreset() const noexcept;
    void setPreset(bool preset) noexcept;

private:
    QString m_noteId;
    QString m_createdAt;
    QString m_author;
    QString m_lastModifiedAt;
    QString m_lastOpenedAt;
    QString m_modifiedBy;
    QStringList m_folders;
    QStringList m_folderUuids;
    QString m_project;
    bool m_bookmarked = false;
    QStringList m_bookmarkColors;
    QStringList m_tags;
    int m_totalFolders = 0;
    int m_totalTags = 0;
    int m_letterCount = 0;
    int m_wordCount = 0;
    int m_sentenceCount = 0;
    int m_paragraphCount = 0;
    int m_spaceCount = 0;
    int m_indentCount = 0;
    int m_lineCount = 0;
    int m_openCount = 0;
    int m_modifiedCount = 0;
    int m_backlinkToCount = 0;
    int m_backlinkByCount = 0;
    int m_includedResourceCount = 0;
    QStringList m_progressEnums;
    int m_progress = -1;
    bool m_preset = false;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
