#include "ThinkingSpace/file/validator/ThinkingSpaceHubStructureValidator.hpp"

#include <QDir>
#include <QFileInfo>

ThinkingSpaceHubStructureValidator::ThinkingSpaceHubStructureValidator() = default;

ThinkingSpaceHubStructureValidator::~ThinkingSpaceHubStructureValidator() = default;

QString ThinkingSpaceHubStructureValidator::normalizePath(const QString& path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    return QDir::cleanPath(trimmed);
}

bool ThinkingSpaceHubStructureValidator::resolveContentsDirectories(
    const QString& tshubPath,
    QStringList* outContentsDirectories,
    QString* errorMessage) const
{
    if (outContentsDirectories == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("outContentsDirectories must not be null.");
        }
        return false;
    }

    outContentsDirectories->clear();

    const QString hubRootPath = normalizePath(tshubPath);
    if (hubRootPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("tshubPath must not be empty.");
        }
        return false;
    }

    const QFileInfo hubInfo(hubRootPath);
    if (!hubInfo.exists())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("tshubPath does not exist: %1").arg(hubRootPath);
        }
        return false;
    }

    if (!hubInfo.isDir() || !hubInfo.fileName().endsWith(QStringLiteral(".tshub"), Qt::CaseInsensitive))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("tshubPath must be an unpacked .tshub directory: %1").arg(hubRootPath);
        }
        return false;
    }

    const QDir hubDir(hubRootPath);
    const QString fixedInternalPath = hubDir.filePath(QStringLiteral(".tscontents"));
    if (QFileInfo(fixedInternalPath).isDir())
    {
        outContentsDirectories->push_back(QDir::cleanPath(fixedInternalPath));
    }

    const QStringList dynamicContentsDirectories = hubDir.entryList(
        QStringList{QStringLiteral("*.tscontents")},
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);
    for (const QString& directoryName : dynamicContentsDirectories)
    {
        outContentsDirectories->push_back(QDir::cleanPath(hubDir.filePath(directoryName)));
    }

    outContentsDirectories->removeDuplicates();
    if (!outContentsDirectories->isEmpty())
    {
        return true;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = QStringLiteral("No *.tscontents directory was found inside .tshub: %1").arg(hubRootPath);
    }
    return false;
}

QStringList ThinkingSpaceHubStructureValidator::resolveLibraryRoots(const QString& tshubPath) const
{
    QStringList contentsDirectories;
    if (!resolveContentsDirectories(tshubPath, &contentsDirectories, nullptr))
    {
        return {};
    }

    QStringList libraryRoots;
    for (const QString& contentsDirectory : contentsDirectories)
    {
        const QDir contentsDir(contentsDirectory);
        const QString fixedLibraryPath = contentsDir.filePath(QStringLiteral("Library.tslibrary"));
        if (QFileInfo(fixedLibraryPath).isDir())
        {
            libraryRoots.push_back(QDir::cleanPath(fixedLibraryPath));
        }

        const QStringList dynamicLibraries = contentsDir.entryList(
            QStringList{QStringLiteral("*.tslibrary")},
            QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QString& directoryName : dynamicLibraries)
        {
            libraryRoots.push_back(QDir::cleanPath(contentsDir.filePath(directoryName)));
        }
    }

    libraryRoots.removeDuplicates();
    return libraryRoots;
}

QString ThinkingSpaceHubStructureValidator::resolvePrimaryLibraryPath(const QString& tshubPath, QString* errorMessage) const
{
    const QStringList libraryRoots = resolveLibraryRoots(tshubPath);
    if (!libraryRoots.isEmpty())
    {
        return libraryRoots.first();
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = QStringLiteral("No Library.tslibrary directory found inside: %1").arg(normalizePath(tshubPath));
    }
    return {};
}

QString ThinkingSpaceHubStructureValidator::resolveHubStatPath(const QString& tshubPath) const
{
    const QString normalizedWshubPath = normalizePath(tshubPath);
    if (normalizedWshubPath.isEmpty())
    {
        return {};
    }

    const QDir hubDir(normalizedWshubPath);
    const QStringList statFiles = hubDir.entryList(
        QStringList{QStringLiteral("*.tsstat")},
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name);
    if (statFiles.isEmpty())
    {
        return {};
    }

    return QDir::cleanPath(hubDir.filePath(statFiles.first()));
}
