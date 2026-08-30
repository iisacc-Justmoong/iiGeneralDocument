#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>

class ThinkingSpaceResourcesHierarchyStore;

class ThinkingSpaceResourcesHierarchyParser
{
public:
    ThinkingSpaceResourcesHierarchyParser();
    ~ThinkingSpaceResourcesHierarchyParser();

    bool parse(
        const QString& rawText,
        ThinkingSpaceResourcesHierarchyStore* outStore,
        QString* errorMessage = nullptr) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
