#include <iiGeneralDocument.h>
#include "TestSupport.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <filesystem>
using namespace ii::document;
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto author = iiFileProvider::FileAuthor::fromIisaccAccount(
        {{"sub", "document-author"}, {"email", "author@example.com"}, {"displayName", "Document Author"}},
        QUrl("https://iisacc.com"), QDateTime::currentDateTimeUtc());
    expect(author.has_value(), "valid author");
    Document pdf; pdf.addPage(Page(Rect{0,0,320,480}));
    expect(pdf.setFileAuthor(*author), "attach PDF author");
    auto revision = pdf.authorship().revision();
    DocumentEditor editor(pdf);
    auto text = editor.addText(0, "Before", Point{10,20}, 12);
    expect(pdf.authorship().revision() == revision + 1, "PDF changes dump synchronously");
    const auto dump = pdf.metadata().at(iiFileProvider::Authorship::MetadataKey);
    editor.replaceText(text,0,"Before"); expect(!editor.remove(ElementId{999}), "missing removal is no-op");
    expect(pdf.metadata().at(iiFileProvider::Authorship::MetadataKey) == dump, "no-op preserves PDF author metadata");
    QTemporaryDir dir(QDir::currentPath()+"/authorship-XXXXXX"); expect(dir.isValid(), "temporary directory");
    const auto root = std::filesystem::path(dir.path().toStdString());
    expect(!PdfDocumentWriter{}.write(pdf,root/"author.pdf").hasErrors(), "write authored PDF");
    auto read = PdfDocumentReader{}.read(root/"author.pdf");
    expect(!read.hasErrors() && read.document.authorship().dump() == pdf.authorship().dump(), "PDF author round trip");
    expect(!read.document.authorship().hasActiveAuthor(), "read cannot select editor");
    WordDocument word; word.setFileAuthor(*author);
    word.appendParagraph(WordParagraph{{},{{"Before",{}}}});
    auto base = word.authorship().revision();
    expect(word.edit([](WordDocument &draft) { std::get<WordParagraph>(draft.blocks()[0]).runs[0].text = "After"; return true; }), "nested Word edit");
    expect(word.authorship().revision() == base+1, "nested mutation dumps before returning");
    const auto stable = word.authorship().dump();
    expect(!word.edit([](WordDocument &draft) { draft.blocks().clear(); return false; }), "reject Word draft");
    expect(!word.edit([](WordDocument &) { return true; }) && word.authorship().dump()==stable, "no-op and rejection preserve Word metadata");
    for (const auto extension : {"docx","odt","fodt","doc"}) {
        const auto path = root / (std::string("author.")+extension);
        expect(!WordDocumentWriter{}.write(word,path).hasErrors(), "write authored Word format");
        auto parsed = WordDocumentReader{}.read(path);
        expect(!parsed.hasErrors() && parsed.document.authorship().dump()==stable, "Word authorship round trip");
    }
    auto html = HtmlBlockDocument::fromHtml("<p>Before</p>"); html.setFileAuthor(*author);
    HtmlBlockEditor htmlEditor(html); htmlEditor.update(html.rootIds()[0],"<p>After</p>");
    expect(html.authorship().revision()==2, "HTML edit dumps synchronously");
    auto htmlRead = HtmlBlockDocument::fromFileBytes(html.toFileBytes());
    expect(htmlRead.html()==html.html() && htmlRead.authorship().dump()==html.authorship().dump(), "HTML file metadata round trip");
    auto xml = XmlTreeDocument::fromXml("<?xml version=\"1.0\"?><root>Before</root>"); xml.setFileAuthor(*author);
    XmlTreeEditor xmlEditor(xml); xmlEditor.update(*xml.rootId(),"<root>After</root>");
    expect(xml.authorship().revision()==2, "XML edit dumps synchronously");
    auto xmlRead = XmlTreeDocument::fromFileBytes(xml.toFileBytes());
    expect(xmlRead.xml()==xml.xml() && xmlRead.authorship().dump()==xml.authorship().dump(), "XML file metadata round trip");
    // OpenDocument defines the omitted value-type as string; LibreOffice uses it.
    QFile fodt(QString::fromStdString((root/"author.fodt").string()));
    expect(fodt.open(QIODevice::ReadOnly), "open authored FODT");
    auto fodtBytes = fodt.readAll(); fodt.close(); fodtBytes.replace(" meta:value-type=\"string\"", "");
    expect(fodt.open(QIODevice::WriteOnly|QIODevice::Truncate), "write default-type FODT"); fodt.write(fodtBytes); fodt.close();
    auto defaultType = WordDocumentReader{}.read(root/"author.fodt");
    expect(!defaultType.hasErrors() && defaultType.document.authorship().dump()==stable, "ODF default string metadata");
    bool rejected = false;
    auto badXml = xml.toFileBytes(); badXml.replace("iisacc:authorship:v1:","iisacc:authorship:v9:");
    try { (void)XmlTreeDocument::fromFileBytes(badXml); } catch (const DocumentError &) { rejected = true; }
    expect(rejected, "future markup authorship schema fails closed");
    ThinkingSpaceDocument thinking; thinking.body.htmlBlocks = HtmlBlockDocument::fromHtml("<p>Body</p>");
    thinking.setFileAuthor(*author);
    (void)thinking.recordVersion("Initial", "2026-09-07T12:00:00Z");
    const auto initialRevision = thinking.authorship().revision();
    expect(thinking.edit([](ThinkingSpaceDocument &draft) { draft.header.metadata["Title"]="Authored note"; return true; }), "header edit");
    expect(thinking.authorship().revision()==initialRevision+1 && thinking.header.metadata.at(iiFileProvider::Authorship::MetadataKey)==thinking.authorship().dump().toStdString(), "header edits dump immediately");
    (void)thinking.recordVersion("Header", "2026-09-07T12:01:00Z");
    for (int index=0; index<101; ++index) {
        thinking.edit([&](ThinkingSpaceDocument &draft) { draft.header.metadata["Step"]=std::to_string(index); return true; });
        (void)thinking.recordVersion("Change", "2026-09-07T12:02:00Z");
    }
    const auto tsdoc = thinking.toFileBytes();
    const auto loaded = ThinkingSpaceDocument::fromFileBytes(tsdoc);
    expect(loaded.authorship().dump()==thinking.authorship().dump() && loaded.versionHistory.verifyIntegrity(), "Thinking Space author and complete history round trip");
    expect(loaded.versionHistory.versions().size()==100 && loaded.versionHistory.prunedVersionCount()==3, "pruned history persists");
    auto damaged = QJsonDocument::fromJson(tsdoc).object(); damaged["schemaVersion"] = 2;
    rejected = false;
    try { (void)ThinkingSpaceDocument::fromFileBytes(QJsonDocument(damaged).toJson()); } catch (const DocumentError &) { rejected = true; }
    expect(rejected, "future tsdoc schema fails closed");
    ThinkingSpaceDocument invalidUtf8;
    invalidUtf8.header.metadata["invalid"] = std::string(1,static_cast<char>(0xff));
    rejected = false;
    try { (void)invalidUtf8.toFileBytes(); } catch (const DocumentError &) { rejected = true; }
    expect(rejected, "invalid UTF-8 cannot be silently rewritten by tsdoc serialization");
    return 0;
}
