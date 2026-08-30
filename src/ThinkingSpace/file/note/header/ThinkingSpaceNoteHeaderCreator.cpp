#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderCreator.hpp"

#include "ThinkingSpace/file/note/header/ThinkingSpaceBookmarkColorPalette.hpp"
#include "ThinkingSpace/file/ThinkingSpaceDebugTrace.hpp"

#include <QFileInfo>

#include <utility>

namespace
{
    QString escapeXmlText(QString value)
    {
        value.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
        value.replace(QStringLiteral("<"), QStringLiteral("&lt;"));
        value.replace(QStringLiteral(">"), QStringLiteral("&gt;"));
        value.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
        value.replace(QStringLiteral("'"), QStringLiteral("&apos;"));
        return value;
    }

    QString boolToText(bool value)
    {
        return value ? QStringLiteral("true") : QStringLiteral("false");
    }

    QStringList defaultProgressEnums()
    {
        return {
            QStringLiteral("Ready"),
            QStringLiteral("Pending"),
            QStringLiteral("InProgress"),
            QStringLiteral("Done")
        };
    }

    QString serializeProgressEnumsAttribute(QStringList progressEnums)
    {
        for (int index = progressEnums.size() - 1; index >= 0; --index)
        {
            progressEnums[index] = progressEnums.at(index).trimmed();
            if (progressEnums.at(index).isEmpty())
            {
                progressEnums.removeAt(index);
            }
        }

        if (progressEnums.isEmpty())
        {
            progressEnums = defaultProgressEnums();
        }

        return QStringLiteral("{%1}").arg(progressEnums.join(QLatin1Char(',')));
    }
} // namespace

ThinkingSpaceNoteHeaderCreator::ThinkingSpaceNoteHeaderCreator(QString workspaceRootPath, QString notesRootPath)
    : ThinkingSpaceNoteCreator(std::move(workspaceRootPath), std::move(notesRootPath))
{
}

ThinkingSpaceNoteHeaderCreator::~ThinkingSpaceNoteHeaderCreator() = default;

QString ThinkingSpaceNoteHeaderCreator::creatorName() const
{
    return QStringLiteral("ThinkingSpaceNoteHeaderCreator");
}

QString ThinkingSpaceNoteHeaderCreator::targetPathForNote(const QString& noteId) const
{
    const QString noteDirPath = noteDirectoryPath(noteId);
    const QString noteStem = QFileInfo(noteDirPath).completeBaseName();
    const QString fileName = noteStem + QStringLiteral(".tsnhead");
    const QString targetPath = joinPath(noteDirPath, fileName);
    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.creator.header"),
                              QStringLiteral("targetPathForNote"),
                              QStringLiteral("noteId=%1 path=%2").arg(noteId, targetPath));
    return targetPath;
}

QStringList ThinkingSpaceNoteHeaderCreator::requiredRelativePaths() const
{
    return {};
}

QString ThinkingSpaceNoteHeaderCreator::headerFileName() const
{
    return QStringLiteral("note.tsnhead");
}

QString ThinkingSpaceNoteHeaderCreator::createHeaderText(const ThinkingSpaceNoteHeaderStore& store) const
{
    const QString noteId = store.noteId().trimmed().isEmpty()
                               ? QStringLiteral("note")
                               : escapeXmlText(store.noteId().trimmed());

    QString text;
    text += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    text += QStringLiteral("<!DOCTYPE THINKINGSPACENOTE>\n");
    text += QStringLiteral("<contents id=\"") + noteId + QStringLiteral("\">\n");
    text += QStringLiteral("  <head>\n");
    text += QStringLiteral("    <meta charset=\"UTF-8\" />\n");
    text += QStringLiteral("    <meta name=\"tsn-type\" content=\"tsnhead\" />\n");
    text += QStringLiteral("    <created>") + escapeXmlText(store.createdAt()) + QStringLiteral("</created>\n");
    text += QStringLiteral("    <author>") + escapeXmlText(store.author()) + QStringLiteral("</author>\n");
    text += QStringLiteral("    <lastModified>") + escapeXmlText(store.lastModifiedAt())
        + QStringLiteral("</lastModified>\n");
    text += QStringLiteral("    <lastOpened>") + escapeXmlText(store.lastOpenedAt())
        + QStringLiteral("</lastOpened>\n");
    text += QStringLiteral("    <modifiedBy>") + escapeXmlText(store.modifiedBy())
        + QStringLiteral("</modifiedBy>\n");

    text += QStringLiteral("    <folders>\n");
    const QStringList folders = store.folders();
    const QStringList folderUuids = store.folderUuids();
    for (int index = 0; index < folders.size(); ++index)
    {
        const QString folder = folders.at(index);
        QString folderTag = QStringLiteral("      <folder");
        if (index < folderUuids.size() && !folderUuids.at(index).trimmed().isEmpty())
        {
            folderTag += QStringLiteral(" uuid=\"") + escapeXmlText(folderUuids.at(index).trimmed())
                + QStringLiteral("\"");
        }
        folderTag += QStringLiteral(">")
            + escapeXmlText(folder)
            + QStringLiteral("</folder>\n");
        text += folderTag;
    }
    text += QStringLiteral("    </folders>\n");

    text += QStringLiteral("    <project>") + escapeXmlText(store.project()) + QStringLiteral("</project>\n");
    QString bookmarkTag = QStringLiteral("    <bookmarks state=\"") + boolToText(store.isBookmarked()) +
        QStringLiteral("\"");
    const QString colorsAttribute = ThinkingSpace::Bookmarks::serializeBookmarkColorsAttribute(store.bookmarkColors());
    if (!colorsAttribute.isEmpty())
    {
        bookmarkTag += QStringLiteral(" colors=\"") + escapeXmlText(colorsAttribute) + QStringLiteral("\"");
    }
    bookmarkTag += QStringLiteral(" />\n");
    text += bookmarkTag;

    text += QStringLiteral("    <tags>\n");
    for (const QString& tag : store.tags())
    {
        text += QStringLiteral("      <tag>") + escapeXmlText(tag) + QStringLiteral("</tag>\n");
    }
    text += QStringLiteral("    </tags>\n");

    text += QStringLiteral("    <fileStat>\n");
    text += QStringLiteral("      <totalFolders>") + QString::number(store.totalFolders())
        + QStringLiteral("</totalFolders>\n");
    text += QStringLiteral("      <totalTags>") + QString::number(store.totalTags())
        + QStringLiteral("</totalTags>\n");
    text += QStringLiteral("      <letterCount>") + QString::number(store.letterCount())
        + QStringLiteral("</letterCount>\n");
    text += QStringLiteral("      <wordCount>") + QString::number(store.wordCount())
        + QStringLiteral("</wordCount>\n");
    text += QStringLiteral("      <sentenceCount>") + QString::number(store.sentenceCount())
        + QStringLiteral("</sentenceCount>\n");
    text += QStringLiteral("      <paragraphCount>") + QString::number(store.paragraphCount())
        + QStringLiteral("</paragraphCount>\n");
    text += QStringLiteral("      <spaceCount>") + QString::number(store.spaceCount())
        + QStringLiteral("</spaceCount>\n");
    text += QStringLiteral("      <indentCount>") + QString::number(store.indentCount())
        + QStringLiteral("</indentCount>\n");
    text += QStringLiteral("      <lineCount>") + QString::number(store.lineCount())
        + QStringLiteral("</lineCount>\n");
    text += QStringLiteral("      <openCount>") + QString::number(store.openCount())
        + QStringLiteral("</openCount>\n");
    text += QStringLiteral("      <modifiedCount>") + QString::number(store.modifiedCount())
        + QStringLiteral("</modifiedCount>\n");
    text += QStringLiteral("      <backlinkToCount>") + QString::number(store.backlinkToCount())
        + QStringLiteral("</backlinkToCount>\n");
    text += QStringLiteral("      <backlinkByCount>") + QString::number(store.backlinkByCount())
        + QStringLiteral("</backlinkByCount>\n");
    text += QStringLiteral("      <includedResourceCount>") + QString::number(store.includedResourceCount())
        + QStringLiteral("</includedResourceCount>\n");
    text += QStringLiteral("    </fileStat>\n");

    const QString progressText = store.progress() < 0 ? QString() : QString::number(store.progress());
    text += QStringLiteral("    <progress enums=\"")
        + escapeXmlText(serializeProgressEnumsAttribute(store.progressEnums()))
        + QStringLiteral("\">")
        + progressText + QStringLiteral("</progress>\n");
    text += QStringLiteral("    <isPreset>") + boolToText(store.isPreset()) + QStringLiteral("</isPreset>\n");
    text += QStringLiteral("  </head>\n");
    text += QStringLiteral("</contents>\n");

    ThinkingSpace::Debug::traceSelf(this,
                              QStringLiteral("note.creator.header"),
                              QStringLiteral("createHeaderText"),
                              QStringLiteral("id=%1 folderCount=%2 tagCount=%3 openCount=%4 modifiedCount=%5 lastOpened=%6 progress=%7 bytes=%8")
                              .arg(store.noteId())
                              .arg(store.folders().size())
                              .arg(store.tags().size())
                              .arg(store.openCount())
                              .arg(store.modifiedCount())
                              .arg(store.lastOpenedAt())
                              .arg(store.progress())
                              .arg(text.toUtf8().size()));
    return text;
}
