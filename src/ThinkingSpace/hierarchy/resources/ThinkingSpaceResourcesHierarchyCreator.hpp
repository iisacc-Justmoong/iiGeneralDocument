#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>

class ThinkingSpaceResourcesHierarchyStore;

class ThinkingSpaceResourcesHierarchyCreator
{
public:
    ThinkingSpaceResourcesHierarchyCreator();
    ~ThinkingSpaceResourcesHierarchyCreator();

    QString targetRelativePath() const;
    QString createText(const ThinkingSpaceResourcesHierarchyStore& store) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
