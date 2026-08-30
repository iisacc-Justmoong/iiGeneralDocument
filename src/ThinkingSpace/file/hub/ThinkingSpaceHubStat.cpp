#include "ThinkingSpace/file/hub/ThinkingSpaceHubStat.hpp"

#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"

#include <algorithm>
#include <utility>

namespace
{
    QStringList sanitizeParticipants(QStringList values)
    {
        QStringList sanitized;
        sanitized.reserve(values.size());

        for (QString& value : values)
        {
            value = value.trimmed();
            if (value.isEmpty())
            {
                continue;
            }
            if (!sanitized.contains(value))
            {
                sanitized.push_back(value);
            }
        }
        return sanitized;
    }

    QVariantMap sanitizeProfileMap(QVariantMap values)
    {
        QVariantMap sanitized;
        for (auto it = values.begin(); it != values.end(); ++it)
        {
            const QString key = it.key().trimmed();
            const QString value = it.value().toString().trimmed();
            if (key.isEmpty() || value.isEmpty())
            {
                continue;
            }
            sanitized.insert(key, value);
        }
        return sanitized;
    }
} // namespace

ThinkingSpaceHubStat::ThinkingSpaceHubStat() = default;

ThinkingSpaceHubStat::~ThinkingSpaceHubStat() = default;

void ThinkingSpaceHubStat::clear()
{
    m_noteCount = 0;
    m_resourceCount = 0;
    m_characterCount = 0;
    m_createdAtUtc.clear();
    m_lastModifiedAtUtc.clear();
    m_participants.clear();
    m_profileLastModifiedAtUtc.clear();

    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hub.stat"),
                              QStringLiteral("clear"));
}

int ThinkingSpaceHubStat::noteCount() const noexcept
{
    return m_noteCount;
}

void ThinkingSpaceHubStat::setNoteCount(int value)
{
    m_noteCount = std::max(0, value);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hub.stat"),
                              QStringLiteral("setNoteCount"),
                              QStringLiteral("value=%1").arg(m_noteCount));
}

int ThinkingSpaceHubStat::resourceCount() const noexcept
{
    return m_resourceCount;
}

void ThinkingSpaceHubStat::setResourceCount(int value)
{
    m_resourceCount = std::max(0, value);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hub.stat"),
                              QStringLiteral("setResourceCount"),
                              QStringLiteral("value=%1").arg(m_resourceCount));
}

int ThinkingSpaceHubStat::characterCount() const noexcept
{
    return m_characterCount;
}

void ThinkingSpaceHubStat::setCharacterCount(int value)
{
    m_characterCount = std::max(0, value);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hub.stat"),
                              QStringLiteral("setCharacterCount"),
                              QStringLiteral("value=%1").arg(m_characterCount));
}

QString ThinkingSpaceHubStat::createdAtUtc() const
{
    return m_createdAtUtc;
}

void ThinkingSpaceHubStat::setCreatedAtUtc(QString value)
{
    m_createdAtUtc = value.trimmed();
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hub.stat"),
                              QStringLiteral("setCreatedAtUtc"),
                              QStringLiteral("value=%1").arg(m_createdAtUtc));
}

QString ThinkingSpaceHubStat::lastModifiedAtUtc() const
{
    return m_lastModifiedAtUtc;
}

void ThinkingSpaceHubStat::setLastModifiedAtUtc(QString value)
{
    m_lastModifiedAtUtc = value.trimmed();
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hub.stat"),
                              QStringLiteral("setLastModifiedAtUtc"),
                              QStringLiteral("value=%1").arg(m_lastModifiedAtUtc));
}

QStringList ThinkingSpaceHubStat::participants() const
{
    return m_participants;
}

void ThinkingSpaceHubStat::setParticipants(QStringList values)
{
    m_participants = sanitizeParticipants(std::move(values));
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hub.stat"),
                              QStringLiteral("setParticipants"),
                              QStringLiteral("count=%1 values=[%2]")
                              .arg(m_participants.size())
                              .arg(m_participants.join(QStringLiteral(", "))));
}

QVariantMap ThinkingSpaceHubStat::profileLastModifiedAtUtc() const
{
    return m_profileLastModifiedAtUtc;
}

void ThinkingSpaceHubStat::setProfileLastModifiedAtUtc(QVariantMap values)
{
    m_profileLastModifiedAtUtc = sanitizeProfileMap(std::move(values));
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hub.stat"),
                              QStringLiteral("setProfileLastModifiedAtUtc"),
                              QStringLiteral("count=%1").arg(m_profileLastModifiedAtUtc.size()));
}
