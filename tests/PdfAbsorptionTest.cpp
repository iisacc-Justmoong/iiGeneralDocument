#include <iiGeneralDocument.h>
#include "PdfFixture.h"
#include "TestSupport.h"
#include <limits>
#include <set>
#include <string>

using namespace ii::document;

Document read(std::string bytes) {
    auto result = PdfDocumentReader{}.readBytes(std::as_bytes(std::span(bytes)), "SDK fixture",
        ReadOptions{.attemptRecovery = false});
    for (const auto& diagnostic : result.diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::error) std::cerr << diagnostic.message << '\n';
    expect(!result.hasErrors(), "Fixture is readable through the existing Document object");
    return std::move(result.document);
}
Document reopen(const Document& document) {
    auto encoded = PdfDocumentWriter{}.writeBytes(document);
    for (const auto& diagnostic : encoded.diagnostics)
        if (diagnostic.severity == DiagnosticSeverity::error) std::cerr << diagnostic.message << '\n';
    expect(!encoded.hasErrors() && !encoded.bytes.empty(), "Document serializes through the SDK byte writer");
    auto result = PdfDocumentReader{}.readBytes(encoded.bytes);
    expect(!result.hasErrors(), "Serialized document can be reopened");
    return std::move(result.document);
}
template<class Fn> void rejected(Fn function) {
    bool threw = false;
    try { function(); } catch (const DocumentError&) { threw = true; }
    expect(threw, "Unsupported edit is rejected");
}
int main() {
    auto document = read(pdfFixture());
    expect(document.pages().size() == 2 && document.hasSourcePdf(), "Source pages are SDK objects");
    expect(document.pages()[0].cropBox().x == -10 && document.pages()[1].cropBox().x == -30,
        "Declared and effective CropBox are accessible");
    expect(document.pages()[1].rotation() == 270 && document.pages()[1].userUnit() == 2,
        "Negative rotation is normalized and UserUnit is accessible");
    expect(document.attachments().size() == 1 && document.attachments()[0].declaredSize == 13,
        "Embedded attachment summaries belong to Document");
    const auto author = iiFileProvider::native::FileAuthor::fromIisaccAccount(
        {{"sub", "pdf-absorption-fixture"}, {"email", "fixture@example.com"}, {"displayName", "Fixture Author"}},
        "https://iisacc.com", iiFileProvider::native::utcNow());
    expect(author.has_value() && document.setFileAuthor(*author), "A fixture author selects the aggregate editing context");
    DocumentEditor editor(document);
    const auto initialRevision = document.authorship().revision();
    const std::size_t second[]{1};
    editor.rotatePages(second, -450);
    expect(document.pages()[1].rotation() == 180 && document.authorship().revision() == initialRevision + 1,
        "Batch rotation records exactly one aggregate change");
    const pdf::MetadataChange changes[]{{"Title", "SDK 문서 📄"}, {"Author", std::nullopt}};
    expect(editor.setMetadataBatch(changes), "Metadata batch applies Unicode values and removals");
    const auto noteId = editor.addTextNote(1, Rect{-10, 40, 20, 25}, "SDK 주석\n📄", "iisacc");
    expect(document.pages()[1].findAnnotation(noteId)->author() == "iisacc", "Note author is an Annotation property");
    expect(editor.setTextField("customer", "대한민국 📄"), "Existing FormField text value is edited");
    const auto revision = document.authorship().revision();
    expect(!editor.setTextField("customer", "대한민국 📄") && !editor.setMetadataBatch(changes), "No-op edits are observable");
    editor.rotatePages(second, 360);
    expect(document.authorship().revision() == revision, "No-op edits do not create authorship changes");

    const auto metadata = document.metadata();
    const std::size_t invalid[]{0, 2}, repeated[]{1, 1};
    rejected([&] { editor.rotatePages(invalid, 90); });
    rejected([&] { editor.rotatePages(repeated, 90); });
    rejected([&] { editor.setTextField("accepted", "value"); });
    rejected([&] { editor.setTextField("customer", std::string("\xC0\xAF", 2)); });
    const pdf::MetadataChange badBatch[]{{"Title", "partial change"}, {"MadeUp", "value"}};
    rejected([&] { editor.setMetadataBatch(badBatch); });
    rejected([&] { editor.addTextNote(1, {0, 0, std::numeric_limits<double>::infinity(), 2}, "note"); });
    expect(document.pages()[0].rotation() == 0 && document.pages()[1].rotation() == 180
        && document.metadata() == metadata && document.authorship().revision() == revision,
        "Rejected batches preserve page state, metadata and authorship");

    auto saved = reopen(document);
    expect(saved.pages()[1].rotation() == 180 && saved.pages()[0].cropBox().x == -10
        && saved.pages()[1].userUnit() == 2, "All page geometry survives save/reopen");
    expect(saved.metadata().at("Title") == "SDK 문서 📄" && !saved.metadata().contains("Author"), "Metadata survives save/reopen");
    expect(saved.pages()[1].annotations().back().contents() == "SDK 주석\n📄"
        && saved.pages()[1].annotations().back().author() == "iisacc", "Note text and author survive save/reopen");
    expect(saved.formFields()[0].utf8Value() == "대한민국 📄" && saved.attachments().size() == 1,
        "Logical form value and embedded attachment survive save/reopen");
    expect(saved.authorship().revision() == revision, "PDF serialization does not add a revision");

    const std::size_t selection[]{1, 0, 0};
    auto extracted = editor.extractPages(selection);
    expect(extracted.pages().size() == 3 && extracted.pages()[0].rotation() == 180
        && extracted.formFields().size() == 3, "Extracted SDK Document preserves order, repetitions and widgets");
    expect(extracted.attachments().empty() && !extracted.metadata().contains("Title"), "Page copy has document-level scope defined explicitly");
    expect(extracted.authorship().revision() == revision + 1 && document.authorship().revision() == revision,
        "Derived document records a change without modifying the source");
    expect(extracted.authorship().hasActiveAuthor(), "Derived document retains the caller's active editing context");
    auto extractedReopened = reopen(extracted);
    expect(extractedReopened.formFields().size() == 3, "Repeated form pages remain serializable");
    auto other = read(pdfFixture());
    const DocumentPageSelection selections[]{{std::cref(document), {1}}, {std::cref(other), {0, 1}}, {std::cref(document), {0}}};
    auto merged = DocumentEditor::mergePages(selections);
    expect(merged.authorship().hasActiveAuthor() && merged.authorship().revision() == revision + 1,
        "Merged aggregate retains the primary document's authorship context");
    expect(merged.pages().size() == 4 && merged.formFields().size() == 4, "Multi-document merge creates an SDK Document");
    std::set<std::string> names;
    for (const auto& field : merged.formFields()) names.insert(field.name());
    expect(names.size() == 4, "Field names remain unique after merging sources");
    auto mergedReopened = reopen(merged);
    expect(mergedReopened.pages().size() == 4 && mergedReopened.pages()[0].rotation() == 180,
        "Merged SDK Document survives another write/read cycle");

    expect(document.removeMetadata("Subject") && !document.removeMetadata("Subject"), "Individual metadata removal tracks a real change");
    rejected([&] { document.removeMetadata(iiFileProvider::native::Authorship::MetadataKey); });
    auto signedDocument = read(pdfFixture(true));
    expect(signedDocument.hasDigitalSignatures(), "ByteRange/DocMDP flags are recognized");
    DocumentEditor signedEditor(signedDocument);
    rejected([&] { signedEditor.rotatePages(second, 90); });
    rejected([&] { auto result = signedEditor.extractPages(second); });
    expect(PdfDocumentWriter{}.writeBytes(signedDocument).hasErrors(), "Signed rewrite is refused by default");
    auto xfaDocument = read(pdfFixture(false, true));
    expect(xfaDocument.hasXfa(), "XFA capability is represented in Document");
    DocumentEditor xfaEditor(xfaDocument);
    rejected([&] { xfaEditor.setTextField("customer", "value"); });
    expect(PdfDocumentWriter{}.writeBytes(xfaDocument).hasErrors(), "XFA rewrite is refused");
    for (const std::string policy : {"/Ff 1", "/Ff 33554432", "/MaxLen 10"}) {
        auto constrained = read(pdfFixture(false, false, policy));
        DocumentEditor constrainedEditor(constrained);
        rejected([&] { constrainedEditor.setTextField("customer", "value"); });
        expect(!PdfDocumentWriter{}.writeBytes(constrained).hasErrors(), "Unchanged constrained fields are preserved");
        constrained.formFields()[0].setUtf8Value("bypass");
        expect(PdfDocumentWriter{}.writeBytes(constrained).hasErrors(), "Direct field changes still obey writer constraints");
        constrained.formFields()[0].setFlags(0);
        constrained.formFields()[0].setType(FormFieldType::button);
        expect(PdfDocumentWriter{}.writeBytes(constrained).hasErrors(), "Mutable model flags/types cannot bypass source text-field constraints");
    }
    Page newPage;
    newPage.setMediaBox({1, 2, 10, 20});
    expect(newPage.cropBox().x == 1, "New-page crop fallback follows media geometry");
    newPage.setCropBox(Rect{0, 0, 0, 20});
    Document invalidDocument; invalidDocument.addPage(std::move(newPage));
    expect(DocumentValidator{}.hasErrors(invalidDocument), "Invalid crop geometry is validated");
    auto inherited = read(pdfFixture(false, false, {}, 90));
    expect(inherited.pages()[0].rotation() == 90, "Inherited source rotation is modeled");
    const std::size_t first[]{0}; DocumentEditor inheritedEditor(inherited);
    inheritedEditor.rotatePages(first, -90);
    auto inheritedReopened = reopen(inherited);
    expect(inheritedReopened.pages()[0].rotation() == 0, "Explicit zero rotation overrides the inherited source rotation");
#ifdef IIGENERALDOCUMENT_TEST_OUTPUT_DIR
    const std::filesystem::path output{IIGENERALDOCUMENT_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(output);
    for (const auto& [name, value] : std::vector<std::pair<std::string, const Document*>>{
             {"edited.pdf", &document}, {"extracted.pdf", &extracted}, {"merged.pdf", &merged}})
        expect(!PdfDocumentWriter{}.write(*value, output / name).hasErrors(), "Assembled SDK objects persist through provider file CRUD");
#endif
    return 0;
}
