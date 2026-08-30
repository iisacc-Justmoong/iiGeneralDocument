#include "TestSupport.h"

#include "ThinkingSpace/editor/GetProperty.h"
#include "ThinkingSpace/editor/SetProperty.h"
#include "ThinkingSpace/editor/SetTag.h"
#include "ThinkingSpace/file/diff/ThinkingSpaceLocalNoteVersionStore.hpp"
#include "ThinkingSpace/file/diff/ThinkingSpaceNoteVersionDiffBuilder.hpp"
#include "ThinkingSpace/file/note/body/ThinkingSpaceNoteBodyPersistence.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderCreator.hpp"
#include "ThinkingSpace/file/note/header/ThinkingSpaceNoteHeaderParser.hpp"
#include "ThinkingSpace/file/note/local/ThinkingSpaceLocalNoteFileStore.hpp"
#include "ThinkingSpace/file/note/package/ThinkingSpaceNoteBodyCreator.hpp"
#include "ThinkingSpace/hierarchy/resources/ThinkingSpaceResourcePackageSupport.hpp"
#include "ThinkingSpace/hierarchy/tags/ThinkingSpaceTagsHierarchyCreator.hpp"
#include "ThinkingSpace/hierarchy/tags/ThinkingSpaceTagsHierarchyParser.hpp"
#include "ThinkingSpace/hierarchy/tags/ThinkingSpaceTagsHierarchyStore.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QVariantMap>

#include <cstdlib>
#include <utility>

namespace {

void expectQt(bool condition, const QString& message)
{
    if (condition) {
        return;
    }
    qCritical().noquote() << "expectation failed:" << message;
    std::exit(1);
}

void bodyPersistenceRoundTripsThinkingSpaceSource()
{
    const QString source = QStringLiteral(
        "Alpha\n<style weight=\"900\">Beta</style>\n#design");
    const QString document =
        ThinkingSpace::NoteBodyPersistence::serializeBodyDocument(QStringLiteral("note-1"), source);

    expectQt(document.contains(QStringLiteral("<!DOCTYPE THINKINGSPACENOTE>")),
             QStringLiteral("Thinking Space body doctype is missing"));
    expectQt(document.contains(QStringLiteral("<contents id=\"note-1\">")),
             QStringLiteral("note id is missing from the body document"));
    expectQt(ThinkingSpace::NoteBodyPersistence::sourceTextFromBodyDocument(document) == source,
             QStringLiteral("body source did not round-trip"));
    expectQt(ThinkingSpace::NoteBodyPersistence::plainTextFromBodyDocument(document)
                 == QStringLiteral("Alpha\nBeta\n#design"),
             QStringLiteral("body plain-text projection changed"));
    expectQt(ThinkingSpace::NoteBodyPersistence::extractedInlineTagValues(source)
                 == QStringList{QStringLiteral("design")},
             QStringLiteral("inline tag extraction changed"));
}

void headerCreatorAndParserRoundTrip()
{
    ThinkingSpaceNoteHeaderStore input;
    input.setNoteId(QStringLiteral("note-1"));
    input.setCreatedAt(QStringLiteral("2026-08-30T10:00:00.000Z"));
    input.setAuthor(QStringLiteral("Thinking Space & iisacc"));
    input.setLastModifiedAt(QStringLiteral("2026-08-30T11:00:00.000Z"));
    input.setLastOpenedAt(QStringLiteral("2026-08-30T12:00:00.000Z"));
    input.setModifiedBy(QStringLiteral("tester"));
    input.setFolderBindings(
        QStringList{QStringLiteral("Work"), QStringLiteral("Archive")},
        QStringList{QStringLiteral("folder-work"), QStringLiteral("folder-archive")});
    input.setProject(QStringLiteral("Thinking Space"));
    input.setBookmarked(true);
    input.setBookmarkColors(QStringList{QStringLiteral("blue"), QStringLiteral("#F59E0B")});
    input.setTags(QStringList{QStringLiteral("design"), QStringLiteral("document")});
    input.setWordCount(4);
    input.setLineCount(2);
    input.setProgressEnums(QStringList{
        QStringLiteral("Ready"), QStringLiteral("InProgress"), QStringLiteral("Done")});
    input.setProgress(1);

    const ThinkingSpaceNoteHeaderCreator creator(QStringLiteral("."), QString());
    const QString text = creator.createHeaderText(input);
    expectQt(text.contains(QStringLiteral("<!DOCTYPE THINKINGSPACENOTE>")),
             QStringLiteral("Thinking Space header doctype is missing"));
    expectQt(text.contains(QStringLiteral("name=\"tsn-type\" content=\"tsnhead\"")),
             QStringLiteral("Thinking Space header type is missing"));

    ThinkingSpaceNoteHeaderStore output;
    QString error;
    expectQt(ThinkingSpaceNoteHeaderParser{}.parse(text, &output, &error), error);
    expectQt(output.noteId() == input.noteId(), QStringLiteral("header note id changed"));
    expectQt(output.author() == input.author(), QStringLiteral("header author changed"));
    expectQt(output.folders() == input.folders(), QStringLiteral("header folders changed"));
    expectQt(output.folderUuids() == input.folderUuids(),
             QStringLiteral("header folder UUIDs changed"));
    expectQt(output.bookmarkColors() == input.bookmarkColors(),
             QStringLiteral("header bookmark colors changed"));
    expectQt(output.tags() == input.tags(), QStringLiteral("header tags changed"));
    expectQt(output.progress() == input.progress(), QStringLiteral("header progress changed"));
}

void editorCommandsPreserveTheOriginalContract()
{
    SetTag tagWriter;
    const QVariantMap tagged = tagWriter.insertNamedTagIntoSource(
        QStringLiteral("header"), QStringLiteral("Alpha Beta"), 6, 4);
    expectQt(tagged.value(QStringLiteral("valid")).toBool(),
             tagged.value(QStringLiteral("errorMessage")).toString());
    expectQt(tagged.value(QStringLiteral("bodySourceText")).toString()
                 == QStringLiteral("Alpha <header>Beta</header>"),
             QStringLiteral("static tag insertion changed"));

    SetProperty propertyWriter;
    const QVariantMap propertyResult = propertyWriter.setPropertyInSource(
        QStringLiteral("<resource />"),
        1,
        QStringLiteral("enabled"),
        QVariant(true));
    expectQt(propertyResult.value(QStringLiteral("valid")).toBool(),
             propertyResult.value(QStringLiteral("errorMessage")).toString());
    expectQt(propertyResult.value(QStringLiteral("bodySourceText")).toString()
                 == QStringLiteral("<resource enabled=true />"),
             QStringLiteral("property serialization changed"));

    GetProperty propertyReader;
    const QVariantMap captured = propertyReader.readPropertiesFromSource(
        propertyResult.value(QStringLiteral("bodySourceText")).toString(), 1);
    expectQt(captured.value(QStringLiteral("valid")).toBool(),
             captured.value(QStringLiteral("errorMessage")).toString());
    expectQt(propertyReader.propertyValue(QStringLiteral("enabled")).toBool(),
             QStringLiteral("typed property capture changed"));
}

void localStoreCreatesAndVersionsTsPackages()
{
    QTemporaryDir workspace;
    expectQt(workspace.isValid(), QStringLiteral("temporary workspace creation failed"));

    const QString noteId = QStringLiteral("versioned-note");
    const QString noteDirectory =
        workspace.filePath(noteId + QStringLiteral(".tsnote"));

    ThinkingSpaceNoteHeaderStore header;
    header.setNoteId(noteId);
    header.setCreatedAt(QStringLiteral("2026-08-30T10:00:00.000Z"));
    header.setAuthor(QStringLiteral("Thinking Space model test"));
    header.setLastModifiedAt(QStringLiteral("2026-08-30T10:00:00.000Z"));
    header.setModifiedBy(QStringLiteral("Thinking Space model test"));

    ThinkingSpaceLocalNoteFileStore store;
    ThinkingSpaceLocalNoteFileStore::CreateRequest createRequest;
    createRequest.noteId = noteId;
    createRequest.noteDirectoryPath = noteDirectory;
    createRequest.headerStore = header;
    createRequest.bodyPlainText = QStringLiteral("Alpha");

    ThinkingSpaceLocalNoteDocument created;
    QString error;
    expectQt(store.createNote(std::move(createRequest), &created, &error), error);
    expectQt(QFileInfo(created.noteHeaderPath).isFile()
                 && created.noteHeaderPath.endsWith(QStringLiteral(".tsnhead")),
             QStringLiteral(".tsnhead file was not created"));
    expectQt(QFileInfo(created.noteBodyPath).isFile()
                 && created.noteBodyPath.endsWith(QStringLiteral(".tsnbody")),
             QStringLiteral(".tsnbody file was not created"));
    expectQt(QFileInfo(created.noteVersionPath).isFile()
                 && created.noteVersionPath.endsWith(QStringLiteral(".tsnversion")),
             QStringLiteral(".tsnversion file was not created"));
    expectQt(QFileInfo(created.notePaintPath).isFile()
                 && created.notePaintPath.endsWith(QStringLiteral(".tsnpaint")),
             QStringLiteral(".tsnpaint file was not created"));

    ThinkingSpaceLocalNoteFileStore::ReadRequest readRequest;
    readRequest.noteId = noteId;
    readRequest.noteDirectoryPath = noteDirectory;
    ThinkingSpaceLocalNoteDocument read;
    error.clear();
    expectQt(store.readNote(readRequest, &read, &error), error);
    expectQt(read.bodySourceText == QStringLiteral("Alpha"),
             QStringLiteral("created note body did not round-trip"));

    read.bodySourceText = QStringLiteral("Beta");
    read.bodyPlainText = QStringLiteral("Beta");
    ThinkingSpaceLocalNoteFileStore::UpdateRequest updateRequest;
    updateRequest.document = read;
    updateRequest.touchLastModified = true;

    ThinkingSpaceLocalNoteDocument updated;
    ThinkingSpaceLocalNoteFileStore::UpdateResult updateResult;
    error.clear();
    expectQt(store.updateNote(std::move(updateRequest), &updated, &error, &updateResult), error);
    expectQt(updateResult.versionDiffPushedToFilesystem,
             QStringLiteral("note update did not persist a version diff"));
    expectQt(updated.headerStore.modifiedCount() == 1,
             QStringLiteral("note modified count did not advance"));

    ThinkingSpaceNoteVersionState versionState;
    error.clear();
    expectQt(ThinkingSpaceLocalNoteVersionStore{}.loadState(
                 updated.noteVersionPath, &versionState, &error),
             error);
    expectQt(versionState.noteId == noteId && versionState.snapshots.size() == 1,
             QStringLiteral("version state did not retain the committed snapshot"));
    expectQt(versionState.snapshots.constFirst().bodyPlainText == QStringLiteral("Beta"),
             QStringLiteral("version snapshot body changed"));
    expectQt(versionState.snapshots.constFirst().bodyDiff.unifiedPatch.contains(
                 QStringLiteral("body.tsnbody")),
             QStringLiteral("version diff retained an obsolete format name"));
}

void versionDiffMergesOntoAChangedCurrentDocument()
{
    const QString base = QStringLiteral("Alpha\nBeta\nGamma");
    const QString incoming = QStringLiteral("Alpha\nBeta remote\nGamma");
    const QString current = QStringLiteral("<bold>Alpha</bold>\nBeta\nGamma");

    ThinkingSpaceNoteVersionDiffBuilder builder;
    const ThinkingSpaceNoteVersionDiffSegment segment =
        builder.diffSegment(base, incoming, QStringLiteral("body.tsnbody"));
    bool applied = false;
    QString error;
    const QString merged = builder.applyDiffSegmentOntoCurrent(
        base, current, segment, &applied, &error);

    expectQt(applied, error);
    expectQt(merged == QStringLiteral("<bold>Alpha</bold>\nBeta remote\nGamma"),
             QStringLiteral("version diff merge changed"));
}

void hierarchyAndPackageNamesUseTs()
{
    const ThinkingSpaceNoteBodyCreator bodyCreator(QStringLiteral("/workspace"));
    expectQt(bodyCreator.targetPathForNote(QStringLiteral("note"))
                 .endsWith(QStringLiteral("note.tsnote/note.tsnbody")),
             QStringLiteral("note body target does not use ts naming"));
    expectQt(ThinkingSpace::Resources::packageDirectorySuffix() == QStringLiteral(".tsresource"),
             QStringLiteral("resource package suffix does not use ts naming"));

    ThinkingSpaceTagsHierarchyStore sourceStore;
    sourceStore.setTagEntries(QVector<ThinkingSpaceTagDepthEntry>{
        {QStringLiteral("design"), QStringLiteral("Design"), 0},
        {QStringLiteral("document"), QStringLiteral("Document"), 1}});
    const ThinkingSpaceTagsHierarchyCreator creator;
    expectQt(creator.targetRelativePath() == QStringLiteral("Tags.tstags"),
             QStringLiteral("tag hierarchy target does not use ts naming"));
    const QString text = creator.createText(sourceStore);
    expectQt(text.contains(QStringLiteral("thinkingspace.tags.flat-v1")),
             QStringLiteral("tag hierarchy schema does not use Thinking Space naming"));

    ThinkingSpaceTagsHierarchyStore parsedStore;
    QString error;
    expectQt(ThinkingSpaceTagsHierarchyParser{}.parse(text, &parsedStore, &error), error);
    expectQt(parsedStore.tagEntries().size() == 2
                 && parsedStore.tagEntries().at(1).depth == 1,
             QStringLiteral("tag hierarchy did not round-trip"));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    bodyPersistenceRoundTripsThinkingSpaceSource();
    headerCreatorAndParserRoundTrip();
    editorCommandsPreserveTheOriginalContract();
    localStoreCreatesAndVersionsTsPackages();
    versionDiffMergesOntoAChangedCurrentDocument();
    hierarchyAndPackageNamesUseTs();
    return 0;
}
