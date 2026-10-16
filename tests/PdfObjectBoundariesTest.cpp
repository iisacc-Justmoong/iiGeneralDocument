#include <iiGeneralDocument/Pdf/Objects.h>
#include "PdfFixture.h"
#include "TestSupport.h"
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <limits>
#include <set>
#include <stdexcept>

namespace pdf = ii::document::pdf;
template<class T> T success(pdf::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void success(pdf::Result<void> result) {
    if (!result) throw std::runtime_error(result.error().message);
}
template<class T> void error(pdf::Result<T> result, pdf::ErrorCode expected) {
    expect(!result, "Invalid request must fail");
    expect(result.error().code == expected, "Failure category must identify the rejected request");
}
pdf::Document fixture(bool signedMarkers = false, bool xfa = false, std::string policy = {}) {
    const auto raw = pdfFixture(signedMarkers, xfa, std::move(policy));
    return success(pdf::Document::fromBytes(std::as_bytes(std::span(raw))));
}
void load(QPDF& q, const pdf::Bytes& bytes) {
    q.setAttemptRecovery(false); q.setSuppressWarnings(true);
    q.processMemoryFile("independent verification", reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
std::string content(QPDF& q, std::size_t index) {
    auto page = QPDFPageDocumentHelper(q).getAllPages().at(index);
    auto stream = page.getObjectHandle().getKey("/Contents");
    if (stream.isArray()) stream = stream.getArrayItem(0);
    const auto buffer = stream.getStreamData();
    return {reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize()};
}
void preserve(const pdf::Document& document) {
    const auto bytes = document.toBytes(); QPDF q; load(q, bytes);
    expect(content(q, 0).find("Page One fixture") != std::string::npos
        && content(q, 1).find("Page Two fixture") != std::string::npos, "Page streams remain intact");
    expect(q.getRoot().getKey("/Metadata").isStream(), "Original XMP remains a stream");
    expect(document.attachments().size() == 1 && document.attachments()[0].declaredSize == 13,
        "Embedded attachment remains present");
    expect(document.pages()[0].cropBox.x0 == -10 && document.pages()[1].userUnit == 2,
        "Original box/unit semantics remain intact");
}
void inspection() {
    auto document = fixture();
    expect(document.version() == "1.7" && document.warnings().empty(), "Version and warnings are inspected");
    expect(document.pages()[0].mediaBox.x1 == 612 && document.pages()[1].cropBox.x0 == -30,
        "Inherited/fallback geometry is inspected");
    expect(document.attachments()[0].filename == "attachment.txt"
        && document.attachments()[0].mimeType == "text/plain", "Attachment metadata is decoded");
    pdf::Editor editor(document);
    const std::size_t pages[]{0, 1}; success(editor.rotatePages(pages, 450)); preserve(document);
    const pdf::MetadataChange metadata[]{{"Title", "제목 📄"}, {"Author", std::nullopt}};
    success(editor.setMetadata(metadata)); preserve(document);
    success(editor.addTextNote(1, {-10.5, 40.25, 9.5, 65.25}, "주석 📄", "작성자")); preserve(document);
    expect(document.pages()[1].annotations.back().author == "작성자", "Native Annotation exposes the note author");
    success(editor.setTextField("customer", "값 📄")); preserve(document);
    const auto bytes = document.toBytes(); QPDF q; load(q, bytes);
    QPDFAcroFormDocumentHelper forms(q);
    for (auto field : forms.getFormFields()) if (field.getFullyQualifiedName() == "customer")
        for (auto widget : forms.getAnnotationsForField(field))
            expect(widget.getObjectHandle().getKey("/AP").isNull(), "Old text-field appearance is removed");
    expect(forms.getNeedAppearances(), "Logical Unicode value requests viewer appearance regeneration");
}
void selection() {
    auto document = fixture(); const auto before = document.toBytes();
    const std::size_t selected[]{1, 0, 0};
    auto extracted = success(document.extractPages(selected));
    const auto bytes = extracted.toBytes(); QPDF q; load(q, bytes);
    expect(content(q, 0).find("Page Two") != std::string::npos
        && content(q, 2).find("Page One") != std::string::npos, "Repeated extraction retains stream order");
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    std::set<QPDFObjGen> widgets;
    for (auto& page : pages) for (auto annotation : page.getAnnotations()) {
        auto handle = annotation.getObjectHandle();
        expect(handle.getKey("/P").getObjGen() == page.getObjectHandle().getObjGen(), "Copied annotation owns the correct page");
        if (annotation.getSubtype() == "/Widget")
            expect(widgets.insert(handle.getObjGen()).second, "Repeated pages have independent widgets");
    }
    QPDFAcroFormDocumentHelper forms(q);
    std::set<QPDFObjGen> reachable;
    for (auto field : forms.getFormFields()) for (auto widget : forms.getAnnotationsForField(field))
        reachable.insert(widget.getObjectHandle().getObjGen());
    expect(reachable == widgets && q.getWarnings().empty(), "Copied widgets are reachable from AcroForm without repair warnings");
    auto other = fixture();
    const pdf::PageSelection selections[]{{std::cref(document), {}}, {std::cref(other), {1, 0}}};
    auto merged = success(pdf::Document::merge(selections));
    expect(merged.pages().size() == 4 && merged.formFields().size() == 4, "Empty merge selection means the whole document");
    expect(document.toBytes() == before, "Page assembly does not mutate its input object");
}
void refusals() {
    auto document = fixture(); pdf::Editor editor(document); const auto before = document.toBytes();
    const std::size_t page[]{0}, invalid[]{0, 2}, repeated[]{0, 0};
    error(editor.rotatePages(invalid, 90), pdf::ErrorCode::invalidInput);
    error(editor.rotatePages(page, 45), pdf::ErrorCode::invalidInput);
    error(editor.rotatePages(repeated, 90), pdf::ErrorCode::invalidInput);
    error(document.extractPages({}), pdf::ErrorCode::invalidInput);
    error(document.extractPages(invalid), pdf::ErrorCode::invalidInput);
    error(pdf::Document::merge({}), pdf::ErrorCode::invalidInput);
    const pdf::MetadataChange badBatch[]{{"Title", "partial change"}, {"MadeUp", "value"}};
    const pdf::MetadataChange duplicate[]{{"Title", "first"}, {"Title", "last"}};
    error(editor.setMetadata(badBatch), pdf::ErrorCode::invalidInput);
    error(editor.setMetadata(duplicate), pdf::ErrorCode::invalidInput);
    error(editor.addTextNote(0, {0, 0, 2, 2}, std::string("a\0b", 3)), pdf::ErrorCode::invalidInput);
    error(editor.addTextNote(0, {0, 0, std::numeric_limits<double>::infinity(), 2}, "note"), pdf::ErrorCode::invalidInput);
    error(editor.setTextField("customer", std::string("\xC0\xAF", 2)), pdf::ErrorCode::invalidInput);
    error(editor.setTextField("missing", "value"), pdf::ErrorCode::invalidInput);
    error(editor.setTextField("accepted", "value"), pdf::ErrorCode::unsupportedFeature);
    expect(document.toBytes() == before && document.metadata().at("Title") == "Original fixture",
        "Every rejected request leaves both bytes and snapshots intact");
    const std::string malformed = "not a PDF";
    error(pdf::Document::fromBytes(std::as_bytes(std::span(malformed))), pdf::ErrorCode::malformedPdf);
    error(pdf::Document::fromBytes({}), pdf::ErrorCode::invalidInput);
}
void guards() {
    const std::size_t page[]{0};
    for (const bool signedMarkers : {true, false}) {
        auto document = fixture(signedMarkers, !signedMarkers); const auto before = document.toBytes();
        pdf::Editor editor(document);
        expect(signedMarkers ? document.hasDigitalSignatures() : document.hasXfa(), "Guard condition is visible on the object");
        const auto expected = signedMarkers ? pdf::ErrorCode::signedInput : pdf::ErrorCode::unsupportedFeature;
        error(editor.rotatePages(page, 90), expected);
        error(editor.setTextField("customer", "value"), expected);
        error(editor.addTextNote(0, {0, 0, 2, 2}, "note"), expected);
        const pdf::MetadataChange metadata[]{{"Title", "value"}};
        error(editor.setMetadata(metadata), expected);
        error(document.extractPages(page), expected);
        const pdf::PageSelection selection[]{{std::cref(document), {}}};
        error(pdf::Document::merge(selection), expected);
        expect(document.toBytes() == before, "Signature/XFA refusal preserves all input bytes");
    }
    auto document = fixture(); const auto bytes = document.toBytes(); QPDF q; load(q, bytes);
    QPDFWriter writer(q); writer.setOutputMemory();
    writer.setR6EncryptionParameters("secret", "owner", true, true, true, true, true, true, qpdf_r3p_full, true);
    writer.write(); const auto buffer = writer.getBufferSharedPointer();
    error(pdf::Document::fromBytes(std::span(reinterpret_cast<const std::byte*>(buffer->getBuffer()), buffer->getSize())),
        pdf::ErrorCode::encryptedInput);
}
void fields() {
    for (const std::string policy : {"/Ff 1", "/Ff 33554432", "/MaxLen 10"}) {
        auto document = fixture(false, false, policy); const auto before = document.toBytes();
        pdf::Editor editor(document);
        error(editor.setTextField("customer", "value"), pdf::ErrorCode::unsupportedFeature);
        expect(document.toBytes() == before, "Constrained text fields remain unchanged");
        if (policy.starts_with("/MaxLen")) expect(document.formFields()[0].maximumLength == 10, "MaxLen appears in the field object");
    }
}
void ownership() {
    auto document = fixture(); auto detached = document.toBytes(); detached.clear();
    expect(document.pages().size() == 2 && !document.toBytes().empty(), "Returned bytes are independent copies");
    auto moved = std::move(document);
    expect(moved.pages().size() == 2, "Object ownership transfers on move");
    bool threw = false; try { (void)document.toBytes(); } catch (const std::logic_error&) { threw = true; }
    expect(threw, "Moved-from object misuse is a defined error");
}
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "A boundary case is required");
        const std::string_view selected = argv[1];
        if (selected == "inspection") inspection();
        else if (selected == "selection") selection();
        else if (selected == "refusals") refusals();
        else if (selected == "guards") guards();
        else if (selected == "fields") fields();
        else if (selected == "ownership") ownership();
        else return 2;
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
