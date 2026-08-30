#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>
#include <QVariantMap>

class ThinkingSpaceHubStat
{
public:
    ThinkingSpaceHubStat();
    ~ThinkingSpaceHubStat();

    void clear();

    int noteCount() const noexcept;
    void setNoteCount(int value);

    int resourceCount() const noexcept;
    void setResourceCount(int value);

    int characterCount() const noexcept;
    void setCharacterCount(int value);

    QString createdAtUtc() const;
    void setCreatedAtUtc(QString value);

    QString lastModifiedAtUtc() const;
    void setLastModifiedAtUtc(QString value);

    QStringList participants() const;
    void setParticipants(QStringList values);

    QVariantMap profileLastModifiedAtUtc() const;
    void setProfileLastModifiedAtUtc(QVariantMap values);

private:
    int m_noteCount = 0;
    int m_resourceCount = 0;
    int m_characterCount = 0;
    QString m_createdAtUtc;
    QString m_lastModifiedAtUtc;
    QStringList m_participants;
    QVariantMap m_profileLastModifiedAtUtc;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
