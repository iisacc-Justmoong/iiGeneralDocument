#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

class ThinkingSpaceLibraryHierarchyStore
{
public:
    ThinkingSpaceLibraryHierarchyStore();
    ~ThinkingSpaceLibraryHierarchyStore();

    void clear();

    QString hubPath() const;
    void setHubPath(QString hubPath);

    QStringList noteIds() const;
    void setNoteIds(QStringList values);

private:
    QString m_hubPath;
    QStringList m_noteIds;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
