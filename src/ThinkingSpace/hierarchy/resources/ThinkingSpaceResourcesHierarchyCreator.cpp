#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcesHierarchyCreator.hpp"

#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcesHierarchyStore.hpp"
#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

ThinkingSpaceResourcesHierarchyCreator::ThinkingSpaceResourcesHierarchyCreator() = default;

ThinkingSpaceResourcesHierarchyCreator::~ThinkingSpaceResourcesHierarchyCreator() = default;

QString ThinkingSpaceResourcesHierarchyCreator::targetRelativePath() const
{
    return QStringLiteral("Resources.tsresources");
}

QString ThinkingSpaceResourcesHierarchyCreator::createText(const ThinkingSpaceResourcesHierarchyStore& store) const
{
    QJsonArray values;
    for (const QString& value : store.resourcePaths())
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("resourcePath"), value);
        values.push_back(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("schema"), QStringLiteral("thinkingspace.resources.list"));
    root.insert(QStringLiteral("resources"), values);

    const QString text = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hierarchy.resources.creator"),
                              QStringLiteral("createText"),
                              QStringLiteral("count=%1 bytes=%2")
                              .arg(store.resourcePaths().size())
                              .arg(text.toUtf8().size()));
    return text;
}
