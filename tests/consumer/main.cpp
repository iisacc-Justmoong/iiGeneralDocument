#include <iiGeneralDocument/iiGeneralDocument.h>
#include <iiHtmlBlock>
#include <iiXml>

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

    auto htmlDocument = ii::document::HtmlBlockDocument::fromHtml(
        "<main><p>consumer</p></main>");
    ii::document::HtmlBlockEditor htmlEditor(htmlDocument);
    const auto mainBlockId = htmlDocument.blocks().front().id();
    const auto createdBlockId = htmlEditor.create("<p>created</p>", mainBlockId);
    const bool createdBlockReadable = htmlEditor.read(createdBlockId) != nullptr;
    htmlEditor.update(createdBlockId, "<article>updated</article>");
    const bool updatedBlockReadable = htmlEditor.read(createdBlockId) != nullptr
        && htmlEditor.read(createdBlockId)->tagName() == "article";
    const bool removedBlock = htmlEditor.remove(createdBlockId);

    auto xmlDocument = ii::document::XmlTreeDocument::fromXml("<root />");
    ii::document::XmlTreeEditor xmlEditor(xmlDocument);
    const auto rootNodeId = *xmlDocument.rootId();
    const auto createdNodeId = xmlEditor.create("<child>created</child>", rootNodeId);
    const bool createdNodeReadable = xmlEditor.read(createdNodeId) != nullptr
        && xmlEditor.read(createdNodeId)->parentId() == rootNodeId;
    xmlEditor.update(createdNodeId, "<renamed enabled=true>updated</renamed>");
    const bool updatedNodeReadable = xmlEditor.read(createdNodeId) != nullptr
        && xmlEditor.read(createdNodeId)->name() == "renamed";
    const bool removedNode = xmlEditor.remove(createdNodeId);

    const iiXml::Parser::TagParser parser;
    const auto parsed = parser.Parse("<p>text</p>");
    const iiHtmlBlock::GetHTML html;
    const QString thinkingSpaceBody =
        ThinkingSpace::NoteBodyPersistence::serializeBodyDocument(
            QStringLiteral("consumer"), QStringLiteral("text"));

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
            && wordDocument.plainText() == "word consumer"
            && unsupportedWordRead.hasErrors()
            && thinkingSpaceBody.contains(QStringLiteral("THINKINGSPACENOTE"))
        ? 0
        : 1;
}
