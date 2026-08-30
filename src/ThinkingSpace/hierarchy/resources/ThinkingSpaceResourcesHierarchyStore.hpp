#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

class ThinkingSpaceResourcesHierarchyStore
{
public:
    ThinkingSpaceResourcesHierarchyStore();
    ~ThinkingSpaceResourcesHierarchyStore();

    void clear();

    QString hubPath() const;
    void setHubPath(QString hubPath);

    QStringList resourcePaths() const;
    void setResourcePaths(QStringList values);
    bool writeToFile(const QString& filePath, QString* errorMessage = nullptr) const;

private:
    QString m_hubPath;
    QStringList m_resourcePaths;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
