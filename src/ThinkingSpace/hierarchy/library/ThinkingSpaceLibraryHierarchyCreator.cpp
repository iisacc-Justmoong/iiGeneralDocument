#include "ThinkingSpace/hierarchy/library/ThinkingSpaceLibraryHierarchyCreator.hpp"

#include "ThinkingSpace/hierarchy/library/ThinkingSpaceLibraryHierarchyStore.hpp"
#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

ThinkingSpaceLibraryHierarchyCreator::ThinkingSpaceLibraryHierarchyCreator() = default;

ThinkingSpaceLibraryHierarchyCreator::~ThinkingSpaceLibraryHierarchyCreator() = default;

QString ThinkingSpaceLibraryHierarchyCreator::targetRelativePath() const
{
    return QStringLiteral("Library.tslibrary/index.tsnindex");
}

QString ThinkingSpaceLibraryHierarchyCreator::createText(const ThinkingSpaceLibraryHierarchyStore& store) const
{
    QJsonArray values;
    for (const QString& value : store.noteIds())
    {
        values.push_back(value);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("schema"), QStringLiteral("thinkingspace.library.index"));
    root.insert(QStringLiteral("notes"), values);

    const QString text = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("hierarchy.library.creator"),
                              QStringLiteral("createText"),
                              QStringLiteral("count=%1 bytes=%2")
                              .arg(store.noteIds().size())
                              .arg(text.toUtf8().size()));
    return text;
}
