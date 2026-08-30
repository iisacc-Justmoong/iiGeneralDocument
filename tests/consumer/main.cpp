#include <iiGeneralDocument/iiGeneralDocument.h>
#include <iiHtmlBlock>
#include <iiXml>

int main()
{
    ii::document::Document document;
    document.addPage(ii::document::Page(ii::document::Rect{0.0, 0.0, 100.0, 100.0}));

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
            && thinkingSpaceBody.contains(QStringLiteral("THINKINGSPACENOTE"))
        ? 0
        : 1;
}
