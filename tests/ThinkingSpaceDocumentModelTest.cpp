#include "TestSupport.h"
#include "ThinkingSpace/ThinkingSpaceDocument.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <type_traits>
#include <vector>

using namespace ii::document;

static_assert(std::is_aggregate_v<ThinkingSpaceDocumentHeader>);
static_assert(std::is_aggregate_v<ThinkingSpaceDocumentBody>);
static_assert(std::is_aggregate_v<ThinkingSpaceDocument>);

namespace {

bool isSha256ObjectId(const std::string& value)
{
    return value.size() == 64
        && std::ranges::all_of(value, [](unsigned char character) {
               return std::isdigit(character) != 0
                   || (character >= static_cast<unsigned char>('a')
                       && character <= static_cast<unsigned char>('f'));
           });
}

ThinkingSpaceDocument makeDocument(std::string bodyText)
{
    ThinkingSpaceDocument document;
    document.header.metadata["title"] = "Thinking Space";
    document.body.htmlBlocks = HtmlBlockDocument::fromHtml(
        "<ts-paragraph style=\"display: block\">" + bodyText + "</ts-paragraph>");
    return document;
}

void declaresDocumentParts()
{
    ThinkingSpaceDocument document = makeDocument("Editable body");

    expect(ThinkingSpaceDocument::fileExtension == ".tsdoc",
           "Thinking Space documents declare the .tsdoc extension");
    expect(document.header.metadata.at("title") == "Thinking Space",
           "header metadata remains separate from the body");
    expect(document.body.htmlBlocks.blocks().size() == 1,
           "the body owns an HTML block document");
    expect(document.body.htmlBlocks.blocks().front().tagName() == "ts-paragraph",
           "custom block tags remain addressable in the body");
    expect(document.versionHistory.versions().empty(),
           "a new document starts without recorded versions");
}

void recordsGitStyleContentAddressedVersions()
{
    ThinkingSpaceDocument document = makeDocument("버전 하나");
    document.header.metadata["description"] = "첫째: 줄\n둘째 줄";
    const ThinkingSpaceDocumentVersion first = document.recordVersion(
        "Initial version", "2026-09-01T00:00:00.000Z");

    expect(document.versionHistory.versions().size() == 1,
           "recording creates one version history entry");
    expect(first.parentObjectId.empty(), "the initial version has no parent commit");
    expect(isSha256ObjectId(first.objectId), "version commit uses a SHA-256 object id");
    expect(isSha256ObjectId(first.snapshotObjectId), "snapshot tree uses a SHA-256 object id");
    expect(isSha256ObjectId(first.diffObjectId), "diff uses a SHA-256 object id");

    const ThinkingSpaceDocumentSnapshot* firstSnapshot =
        document.versionHistory.findSnapshot(first.snapshotObjectId);
    const ThinkingSpaceDocumentDiff* firstDiff =
        document.versionHistory.findDiff(first.diffObjectId);
    expect(firstSnapshot != nullptr, "version resolves its immutable snapshot object");
    expect(firstDiff != nullptr, "version resolves its diff object");
    expect(firstSnapshot->headerMetadata.at("title") == "Thinking Space",
           "snapshot owns the recorded header metadata");
    expect(firstSnapshot->bodyHtml == document.body.htmlBlocks.html(),
           "snapshot owns the exact recorded body source");
    expect(firstDiff->targetSnapshotObjectId == first.snapshotObjectId,
           "initial diff targets the initial snapshot");
    const auto initialBody = firstDiff->body.apply("");
    expect(initialBody.has_value() && *initialBody == firstSnapshot->bodyHtml,
           "initial diff applies from the empty-tree body");
    const auto initialSnapshot = firstDiff->apply(ThinkingSpaceDocumentSnapshot{});
    expect(initialSnapshot.has_value()
               && initialSnapshot->objectId == firstSnapshot->objectId,
           "initial diff reconstructs header and body from the empty snapshot");

    document.header.metadata["title"] = "Thinking Space 2";
    document.header.metadata["description"] = "변경: 한 줄";
    document.body.htmlBlocks = HtmlBlockDocument::fromHtml(
        "<ts-paragraph style=\"display: block\">버전 둘</ts-paragraph>");
    const ThinkingSpaceDocumentVersion second = document.recordVersion(
        "Edit body", "2026-09-01T00:01:00.000Z");

    expect(second.parentObjectId == first.objectId,
           "new version points to the previous version commit");
    const ThinkingSpaceDocumentDiff* secondDiff =
        document.versionHistory.findDiff(second.diffObjectId);
    const ThinkingSpaceDocumentSnapshot* secondSnapshot =
        document.versionHistory.findSnapshot(second.snapshotObjectId);
    expect(secondDiff != nullptr && secondSnapshot != nullptr,
           "second version resolves diff and snapshot objects");
    expect(secondDiff->baseSnapshotObjectId == first.snapshotObjectId,
           "diff records the parent snapshot as its base");
    expect(secondDiff->targetSnapshotObjectId == second.snapshotObjectId,
           "diff records the current snapshot as its target");
    const auto updatedBody = secondDiff->body.apply(firstSnapshot->bodyHtml);
    expect(updatedBody.has_value() && *updatedBody == secondSnapshot->bodyHtml,
           "body delta reconstructs the exact target from its base");
    const auto updatedSnapshot = secondDiff->apply(*firstSnapshot);
    expect(updatedSnapshot.has_value()
               && updatedSnapshot->objectId == secondSnapshot->objectId
               && updatedSnapshot->headerMetadata == secondSnapshot->headerMetadata,
           "document diff reconstructs and verifies the complete target snapshot");
    ThinkingSpaceDocumentSnapshot wrongBase = *firstSnapshot;
    wrongBase.bodyHtml += "corrupted";
    expect(!secondDiff->apply(wrongBase).has_value(),
           "document diff rejects a base whose content no longer matches its object id");
    expect(!secondDiff->header.empty(), "metadata changes produce a header delta");
    expect(document.versionHistory.head() != nullptr
               && document.versionHistory.head()->objectId == second.objectId,
           "history head points to the newest version");
    expect(document.versionHistory.verifyIntegrity(),
           "content ids and parent/diff/snapshot links verify as one object graph");
}

void derivesDeterministicObjectIdsFromContent()
{
    ThinkingSpaceDocument first = makeDocument("Same content");
    ThinkingSpaceDocument second = makeDocument("Same content");

    const ThinkingSpaceDocumentVersion firstVersion = first.recordVersion(
        "Same commit", "2026-09-01T01:00:00.000Z");
    const ThinkingSpaceDocumentVersion secondVersion = second.recordVersion(
        "Same commit", "2026-09-01T01:00:00.000Z");

    expect(firstVersion.snapshotObjectId == secondVersion.snapshotObjectId,
           "equal header and body content produce the same snapshot id");
    expect(firstVersion.diffObjectId == secondVersion.diffObjectId,
           "equal base and target content produce the same diff id");
    expect(firstVersion.objectId == secondVersion.objectId,
           "equal commit content produces the same version id");
}

void retainsOnlyTheNewestOneHundredVersions()
{
    ThinkingSpaceDocument document = makeDocument("Version 0");
    std::vector<std::string> versionIds;
    versionIds.reserve(102);

    for (int index = 1; index <= 102; ++index) {
        document.body.htmlBlocks = HtmlBlockDocument::fromHtml(
            "<ts-paragraph style=\"display: block\">Version "
            + std::to_string(index) + "</ts-paragraph>");
        const ThinkingSpaceDocumentVersion version = document.recordVersion(
            "version-" + std::to_string(index),
            "2026-09-01T02:00:00.000Z");
        versionIds.push_back(version.objectId);
    }

    const auto& versions = document.versionHistory.versions();
    expect(ThinkingSpaceDocumentVersionHistory::maximumVersionCount == 100,
           "Thinking Space version history limit is exactly one hundred");
    expect(versions.size() == ThinkingSpaceDocumentVersionHistory::maximumVersionCount,
           "history never retains more than one hundred versions");
    expect(versions.front().label == "version-3",
           "overflow removes the oldest version first");
    expect(versions.back().label == "version-102",
           "newest version remains at history head");
    expect(document.versionHistory.findVersion(versionIds[0]) == nullptr
               && document.versionHistory.findVersion(versionIds[1]) == nullptr,
           "pruned version commits are no longer retained");
    expect(document.versionHistory.prunedVersionCount() == 2,
           "history reports the number of deleted old versions");
    expect(document.versionHistory.hasPrunedHistory(),
           "history records that its parent chain has a shallow boundary");
    expect(document.versionHistory.shallowBoundaryParentObjectId() == versionIds[1],
           "shallow boundary preserves the immutable id of the deleted parent");
    expect(document.versionHistory.snapshots().size() <= 100
               && document.versionHistory.diffs().size() <= 100,
           "unreferenced snapshot and diff objects are garbage-collected with old versions");
    expect(document.versionHistory.verifyIntegrity(),
           "retained content-addressed history verifies after pruning");
}

} // namespace

int main()
{
    declaresDocumentParts();
    recordsGitStyleContentAddressedVersions();
    derivesDeterministicObjectIdsFromContent();
    retainsOnlyTheNewestOneHundredVersions();
}
