#include "Core/Diagnostic.h"
#include "Html/HtmlBlockDocument.h"
#include "Html/HtmlBlockEditor.h"
#include "TestSupport.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

using namespace ii::document;

namespace {

const HtmlBlock& findByValue(const HtmlBlockDocument& document, const std::string& value)
{
    for (const auto& block : document.blocks()) {
        if (block.value() == value) {
            return block;
        }
    }
    throw DocumentError("HTML block test value was not found");
}

const HtmlBlock& findByTag(const HtmlBlockDocument& document, const std::string& tagName)
{
    for (const auto& block : document.blocks()) {
        if (block.tagName() == tagName) {
            return block;
        }
    }
    throw DocumentError("HTML block test tag was not found");
}

template<typename Function>
void expectDocumentError(Function&& function, const char* message)
{
    try {
        function();
    } catch (const DocumentError&) {
        return;
    }
    expect(false, message);
}

void parsesAndReadsTrackedBlocks()
{
    const std::string source =
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE html>\n"
        "<html><body><section><p lang=\"ko\">하나</p><p>Two</p></section></body></html>";
    HtmlBlockDocument document = HtmlBlockDocument::fromHtml(source);

    expect(document.html() == source, "HTML source is preserved byte-for-byte on read");
    expect(document.blocks().size() == 5, "iiHtmlBlock identifies nested HTML block elements");
    expect(document.revision() == 0, "loading does not count as a mutation");

    std::unordered_set<std::uint64_t> ids;
    for (const auto& block : document.blocks()) {
        expect(block.id().value != 0, "every parsed block receives a non-zero id");
        ids.insert(block.id().value);
        expect(block.rawBegin() < block.rawEnd(), "block raw ranges are non-empty");
        expect(source.substr(block.rawBegin(), block.rawEnd() - block.rawBegin()) == block.html(),
               "block HTML matches its source range");
    }
    expect(ids.size() == document.blocks().size(), "block ids are document-wide unique");

    HtmlBlockEditor editor(document);
    const auto& korean = findByValue(document, "하나");
    expect(editor.read(korean.id()) == &korean, "read returns the addressed HTML block");
    expect(editor.read(HtmlBlockId{9999}) == nullptr, "read returns null for an unknown id");

    expectDocumentError(
        [] { static_cast<void>(HtmlBlockDocument::fromHtml("<main>broken")); },
        "document creation rejects malformed block markup");
}

void createsTopLevelAndChildBlocks()
{
    auto document = HtmlBlockDocument::fromHtml("<main><p>One</p></main>");
    HtmlBlockEditor editor(document);
    const HtmlBlockId mainId = findByTag(document, "main").id();
    const HtmlBlockId oneId = findByValue(document, "One").id();

    const HtmlBlockId twoId = editor.create("  <p>Two</p>  ", mainId);
    expect(document.html() == "<main><p>One</p><p>Two</p></main>",
           "child creation appends before the parent closing tag");
    expect(editor.read(mainId) != nullptr, "parent id survives child creation");
    expect(editor.read(oneId) != nullptr, "existing child id survives sibling creation");
    expect(editor.read(twoId) != nullptr && editor.read(twoId)->value() == "Two",
           "create returns the new root block id");
    expect(document.revision() == 1, "successful create increments revision exactly once");

    const HtmlBlockId cardId = editor.create(
        "<card style=\"display: block\"><span>Three</span></card>");
    expect(document.html() ==
               "<main><p>One</p><p>Two</p></main>"
               "<card style=\"display: block\"><span>Three</span></card>",
           "top-level creation appends one serialized block fragment");
    expect(editor.read(cardId) != nullptr && editor.read(cardId)->tagName() == "card",
           "iiHtmlBlock display overrides make custom tags addressable blocks");
    expect(editor.read(cardId)->hasDisplayOverride(), "display override metadata is retained");
    expect(document.revision() == 2, "each successful create has one revision");
}

void createsTheFirstBlockInAnEmptyDocument()
{
    HtmlBlockDocument document;
    HtmlBlockEditor editor(document);

    const HtmlBlockId created = editor.create("<p>First</p>");
    expect(document.html() == "<p>First</p>", "create initializes an empty document");
    expect(editor.read(created) != nullptr && editor.read(created)->value() == "First",
           "the first created block is immediately readable");
    expect(document.revision() == 1, "first create increments revision once");
}

void updatesOneBlockAndPreservesUnaffectedIds()
{
    auto document = HtmlBlockDocument::fromHtml(
        "<section><p>One</p><p>Two</p></section>");
    HtmlBlockEditor editor(document);
    const HtmlBlockId sectionId = findByTag(document, "section").id();
    const HtmlBlockId oneId = findByValue(document, "One").id();
    const HtmlBlockId twoId = findByValue(document, "Two").id();

    editor.update(oneId, "<article><h2>하나</h2><p>Updated</p></article>");

    expect(document.html() ==
               "<section><article><h2>하나</h2><p>Updated</p></article>"
               "<p>Two</p></section>",
           "update replaces exactly one block range");
    expect(editor.read(oneId) != nullptr && editor.read(oneId)->tagName() == "article",
           "the updated block retains its stable identity even when its tag changes");
    expect(editor.read(sectionId) != nullptr, "ancestor identity survives descendant update");
    expect(editor.read(twoId) != nullptr && editor.read(twoId)->value() == "Two",
           "unrelated shifted block identity survives update");
    expect(findByValue(document, "하나").id() != oneId,
           "new nested blocks receive independent identities");
    expect(document.revision() == 1, "successful update increments revision exactly once");
}

void identicalUpdateIsANoOp()
{
    auto document = HtmlBlockDocument::fromHtml(
        "<section><article><p>Same</p></article></section>");
    HtmlBlockEditor editor(document);
    const HtmlBlockId articleId = findByTag(document, "article").id();
    std::vector<HtmlBlockId> ids;
    for (const auto& block : document.blocks()) {
        ids.push_back(block.id());
    }

    editor.update(articleId, "  <article><p>Same</p></article>  ");

    std::vector<HtmlBlockId> resultingIds;
    for (const auto& block : document.blocks()) {
        resultingIds.push_back(block.id());
    }
    expect(document.revision() == 0, "identical update does not create a revision");
    expect(resultingIds == ids, "identical update preserves all descendant identities");
}

void removesOneBlockAndItsDescendants()
{
    auto document = HtmlBlockDocument::fromHtml(
        "<section><article><p>Nested</p></article><p>Keep</p></section>");
    HtmlBlockEditor editor(document);
    const HtmlBlockId sectionId = findByTag(document, "section").id();
    const HtmlBlockId articleId = findByTag(document, "article").id();
    const HtmlBlockId nestedId = findByValue(document, "Nested").id();
    const HtmlBlockId keepId = findByValue(document, "Keep").id();

    expect(editor.remove(articleId), "remove deletes an existing block");
    expect(document.html() == "<section><p>Keep</p></section>",
           "remove erases exactly the selected raw range");
    expect(editor.read(articleId) == nullptr && editor.read(nestedId) == nullptr,
           "removing a parent removes its nested block identities");
    expect(editor.read(sectionId) != nullptr && editor.read(keepId) != nullptr,
           "ancestor and unrelated identities survive removal");
    expect(document.revision() == 1, "successful remove increments revision exactly once");

    expect(!editor.remove(HtmlBlockId{9999}), "remove reports an unknown id without mutation");
    expect(document.revision() == 1, "missing remove does not increment revision");

    expect(editor.remove(sectionId), "the final root block can be removed");
    expect(document.html().empty() && document.blocks().empty(),
           "removing the final root creates a valid empty HTML block document");
    expect(document.revision() == 2, "final-root removal increments revision once");
}

void preservesDocumentPrefixWhenRemovingTheFinalBlock()
{
    const std::string prefix =
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE html>\n";
    auto document = HtmlBlockDocument::fromHtml(prefix + "<main>Only</main>");
    HtmlBlockEditor editor(document);
    const HtmlBlockId rootId = findByTag(document, "main").id();

    expect(editor.remove(rootId), "the prefixed root block is removable");
    expect(document.html() == prefix, "XML declaration and DOCTYPE survive final-block removal");
    expect(document.blocks().empty(), "a prefix-only document has no block objects");
}

void neverReusesDeletedBlockIds()
{
    auto document = HtmlBlockDocument::fromHtml("<main><p>Old</p></main>");
    HtmlBlockEditor editor(document);
    const HtmlBlockId mainId = findByTag(document, "main").id();
    const HtmlBlockId oldId = findByValue(document, "Old").id();

    expect(editor.remove(oldId), "old child is removed before identity reuse check");
    const HtmlBlockId newId = editor.create("<p>New</p>", mainId);

    expect(newId != oldId, "deleted HTML block ids are never reused");
    expect(editor.read(oldId) == nullptr && editor.read(newId) != nullptr,
           "new block identity cannot alias the deleted block");
}

void rejectsEditsThatCrossIiXmlOverlayRanges()
{
    const std::string source =
        "<section><article>One</section><div>Two</article></div>";
    auto document = HtmlBlockDocument::fromHtml(source);
    HtmlBlockEditor editor(document);
    const HtmlBlockId sectionId = findByTag(document, "section").id();

    expectDocumentError(
        [&] { editor.update(sectionId, "<section>Changed</section>"); },
        "an edit that cuts through an iiXml overlay block is rejected");
    expect(document.html() == source, "cross-range rejection leaves source unchanged");
    expect(document.revision() == 0, "cross-range rejection leaves revision unchanged");
}

void rejectsInvalidMutationsAtomically()
{
    auto document = HtmlBlockDocument::fromHtml("<main><p>Safe</p></main>");
    HtmlBlockEditor editor(document);
    const HtmlBlockId mainId = findByTag(document, "main").id();
    const HtmlBlockId safeId = findByValue(document, "Safe").id();
    const std::string originalHtml = document.html();
    const std::size_t originalCount = document.blocks().size();

    expectDocumentError(
        [&] { static_cast<void>(editor.create("<span>inline</span>")); },
        "create rejects a fragment without a block root");
    expectDocumentError(
        [&] { static_cast<void>(editor.create("<p>A</p><p>B</p>")); },
        "create rejects multiple top-level blocks");
    expectDocumentError(
        [&] { static_cast<void>(editor.create("<p>Missing parent</p>", HtmlBlockId{9999})); },
        "create rejects a missing parent id");
    expectDocumentError(
        [&] { editor.update(safeId, "<p>broken"); },
        "update rejects malformed markup");
    expectDocumentError(
        [&] { editor.update(HtmlBlockId{9999}, "<p>Missing</p>"); },
        "update rejects a missing target id");

    expect(document.html() == originalHtml, "failed CRUD leaves source HTML unchanged");
    expect(document.blocks().size() == originalCount, "failed CRUD leaves block state unchanged");
    expect(document.revision() == 0, "failed CRUD leaves revision unchanged");
    expect(editor.read(mainId) != nullptr && editor.read(safeId) != nullptr,
           "failed CRUD leaves existing identities readable");
}

} // namespace

int main()
{
    parsesAndReadsTrackedBlocks();
    createsTopLevelAndChildBlocks();
    createsTheFirstBlockInAnEmptyDocument();
    updatesOneBlockAndPreservesUnaffectedIds();
    identicalUpdateIsANoOp();
    removesOneBlockAndItsDescendants();
    preservesDocumentPrefixWhenRemovingTheFinalBlock();
    neverReusesDeletedBlockIds();
    rejectsEditsThatCrossIiXmlOverlayRanges();
    rejectsInvalidMutationsAtomically();
}
