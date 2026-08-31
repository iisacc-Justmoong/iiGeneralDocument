#include "TestSupport.h"
#include "ThinkingSpace/ThinkingSpaceDocument.h"

#include <type_traits>

using namespace ii::document;

static_assert(std::is_aggregate_v<ThinkingSpaceDocumentHeader>);
static_assert(std::is_aggregate_v<ThinkingSpaceDocumentBody>);
static_assert(std::is_aggregate_v<ThinkingSpaceDocument>);

int main()
{
    ThinkingSpaceDocument document;
    document.header.metadata["title"] = "Thinking Space";
    document.body.htmlBlocks = HtmlBlockDocument::fromHtml(
        "<ts-paragraph style=\"display: block\">Editable body</ts-paragraph>");

    expect(ThinkingSpaceDocument::fileExtension == ".tsdoc",
           "Thinking Space documents declare the .tsdoc extension");
    expect(document.header.metadata.at("title") == "Thinking Space",
           "header metadata remains separate from the body");
    expect(document.body.htmlBlocks.blocks().size() == 1,
           "the body owns an HTML block document");
    expect(document.body.htmlBlocks.blocks().front().tagName() == "ts-paragraph",
           "custom block tags remain addressable in the body");
}
