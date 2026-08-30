#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>

class ThinkingSpaceLibraryHierarchyStore;

class ThinkingSpaceLibraryHierarchyCreator
{
public:
    ThinkingSpaceLibraryHierarchyCreator();
    ~ThinkingSpaceLibraryHierarchyCreator();

    QString targetRelativePath() const;
    QString createText(const ThinkingSpaceLibraryHierarchyStore& store) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
