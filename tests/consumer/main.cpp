#include <iiGeneralDocument/iiGeneralDocument.h>
#include <iiHtmlBlock>
#include <iiXml>

#include <QTemporaryDir>

#include <filesystem>
#include <utility>

int main()
{
    ii::document::Document document;
    document.addPage(ii::document::Page(ii::document::Rect{0.0, 0.0, 100.0, 100.0}));
    ii::document::WordDocument wordDocument;
    ii::document::WordParagraph wordParagraph;
    wordParagraph.runs.push_back({"word consumer"});
    wordDocument.appendParagraph(std::move(wordParagraph));
    const auto unsupportedWordRead =
        ii::document::WordDocumentReader{}.read("unsupported.txt");
    QTemporaryDir odfDirectory;
    if (!odfDirectory.isValid()) {
        return 1;
    }
    const auto odfDirectoryPath = std::filesystem::path(
        odfDirectory.path().toStdString());
    const auto odtPath = odfDirectoryPath / "install-consumer.odt";
    const auto fodtPath = odfDirectoryPath / "install-consumer.fodt";
    const auto odtWrite = ii::document::WordDocumentWriter{}.write(wordDocument, odtPath);
    const auto fodtWrite = ii::document::WordDocumentWriter{}.write(wordDocument, fodtPath);
    const auto odtRead = ii::document::WordDocumentReader{}.read(odtPath);
    const auto fodtRead = ii::document::WordDocumentReader{}.read(fodtPath);

    auto htmlDocument = ii::document::HtmlBlockDocument::fromHtml(
        "<main><p>consumer</p></main>");
    ii::document::HtmlBlockEditor htmlEditor(htmlDocument);
    const auto mainBlockId = htmlDocument.blocks().front().id();
    const auto createdBlockId = htmlEditor.create("<p>created</p>", mainBlockId);
    const bool createdBlockReadable = htmlEditor.read(createdBlockId) != nullptr
        && htmlEditor.read(createdBlockId)->parentId() == mainBlockId
        && htmlEditor.read(createdBlockId)->depth() == 1
        && htmlEditor.read(createdBlockId)->openingTag() == "<p>"
        && htmlEditor.read(createdBlockId)->closingTag() == "</p>"
        && htmlDocument.rootIds().size() == 1
        && htmlDocument.rootIds().front() == mainBlockId;
    htmlEditor.update(createdBlockId, "<article>updated</article>");
    const bool updatedBlockReadable = htmlEditor.read(createdBlockId) != nullptr
        && htmlEditor.read(createdBlockId)->tagName() == "article";
    const bool removedBlock = htmlEditor.remove(createdBlockId);

    auto xmlDocument = ii::document::XmlTreeDocument::fromXml("<root />");
    ii::document::XmlTreeEditor xmlEditor(xmlDocument);
    const auto rootNodeId = *xmlDocument.rootId();
    const auto createdNodeId = xmlEditor.create("<child>created</child>", rootNodeId);
    const bool createdNodeReadable = xmlEditor.read(createdNodeId) != nullptr
        && xmlEditor.read(createdNodeId)->parentId() == rootNodeId
        && xmlEditor.read(createdNodeId)->depth() == 1
        && xmlEditor.read(createdNodeId)->openingTag() == "<child>"
        && xmlEditor.read(createdNodeId)->closingTag() == "</child>";
    xmlEditor.update(createdNodeId, "<renamed enabled=true>updated</renamed>");
    const bool updatedNodeReadable = xmlEditor.read(createdNodeId) != nullptr
        && xmlEditor.read(createdNodeId)->name() == "renamed";
    const bool removedNode = xmlEditor.remove(createdNodeId);

    ii::document::ThinkingSpaceDocument thinkingSpaceDocument;
    thinkingSpaceDocument.header.metadata["title"] = "consumer";
    thinkingSpaceDocument.body.htmlBlocks =
        ii::document::HtmlBlockDocument::fromHtml("<main>versioned</main>");
    const auto thinkingSpaceVersion = thinkingSpaceDocument.recordVersion(
        "consumer-version", "2026-09-01T00:00:00.000Z");
    const bool thinkingSpaceHistoryReadable =
        thinkingSpaceDocument.versionHistory.head() != nullptr
        && thinkingSpaceDocument.versionHistory.head()->objectId
            == thinkingSpaceVersion.objectId
        && thinkingSpaceDocument.versionHistory.verifyIntegrity();

    const iiXml::Parser::TagParser parser;
    const auto parsed = parser.Parse("<p>text</p>");
    const iiHtmlBlock::GetHTML html;
    return document.pages().size() == 1
            && parsed.has_value()
            && parsed->TagName == "p"
            && html.GetHTMLText().empty()
            && createdBlockReadable
            && updatedBlockReadable
            && removedBlock
            && htmlEditor.read(createdBlockId) == nullptr
            && htmlDocument.html() == "<main><p>consumer</p></main>"
            && createdNodeReadable
            && updatedNodeReadable
            && removedNode
            && xmlEditor.read(createdNodeId) == nullptr
            && xmlDocument.xml() == "<root ></root>"
            && thinkingSpaceHistoryReadable
            && wordDocument.plainText() == "word consumer"
            && !odtWrite.hasErrors()
            && !fodtWrite.hasErrors()
            && !odtRead.hasErrors()
            && !fodtRead.hasErrors()
            && odtRead.document.plainText() == "word consumer"
            && fodtRead.document.plainText() == "word consumer"
            && unsupportedWordRead.hasErrors()
        ? 0
        : 1;
}
