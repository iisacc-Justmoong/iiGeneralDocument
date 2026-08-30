#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(default)
#endif

#include <QString>
#include <QStringList>

class ThinkingSpaceHubStructureValidator final
{
public:
    ThinkingSpaceHubStructureValidator();
    ~ThinkingSpaceHubStructureValidator();

    bool resolveContentsDirectories(
        const QString& tshubPath,
        QStringList* outContentsDirectories,
        QString* errorMessage = nullptr) const;
    QStringList resolveLibraryRoots(const QString& tshubPath) const;
    QString resolvePrimaryLibraryPath(const QString& tshubPath, QString* errorMessage = nullptr) const;
    QString resolveHubStatPath(const QString& tshubPath) const;

private:
    QString normalizePath(const QString& path) const;
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif
