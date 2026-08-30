#include "ThinkingSpace/hierarchy/library/ThinkingSpaceLibraryHierarchyStore.hpp"

#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"

#include <utility>

namespace
{
    QStringList sanitizeValues(QStringList values)
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
            sanitized.push_back(value);
        }

        return sanitized;
    }
} // namespace

ThinkingSpaceLibraryHierarchyStore::ThinkingSpaceLibraryHierarchyStore() = default;

ThinkingSpaceLibraryHierarchyStore::~ThinkingSpaceLibraryHierarchyStore() = default;

void ThinkingSpaceLibraryHierarchyStore::clear()
{
    m_hubPath.clear();
    m_noteIds.clear();
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hierarchy.library.store"),
                              QStringLiteral("clear"));
}

QString ThinkingSpaceLibraryHierarchyStore::hubPath() const
{
    return m_hubPath;
}

void ThinkingSpaceLibraryHierarchyStore::setHubPath(QString hubPath)
{
    m_hubPath = hubPath.trimmed();
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hierarchy.library.store"),
                              QStringLiteral("setHubPath"),
                              QStringLiteral("value=%1").arg(m_hubPath));
}

QStringList ThinkingSpaceLibraryHierarchyStore::noteIds() const
{
    return m_noteIds;
}

void ThinkingSpaceLibraryHierarchyStore::setNoteIds(QStringList values)
{
    const int rawCount = values.size();
    m_noteIds = sanitizeValues(std::move(values));
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hierarchy.library.store"),
                              QStringLiteral("setNoteIds"),
                              QStringLiteral("rawCount=%1 sanitizedCount=%2 values=[%3]")
                              .arg(rawCount)
                              .arg(m_noteIds.size())
                              .arg(m_noteIds.join(QStringLiteral(", "))));
}
