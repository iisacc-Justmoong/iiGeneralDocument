#include "Editing/DocumentEditor.h"
#include "Model/Document.h"
#include "Pdf/PdfDocumentReader.h"
#include "Pdf/PdfDocumentWriter.h"
#include "TestSupport.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

using namespace ii::document;

namespace {

const TextElement* firstText(const Document& document)
{
    for (const auto& element : document.pages().front().elements()) {
        if (const auto* text = dynamic_cast<const TextElement*>(element.get())) {
            return text;
        }
    }
    return nullptr;
}

bool hasKind(const Document& document, ElementKind kind)
{
    for (const auto& element : document.pages().front().elements()) {
        if (element->kind() == kind) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    const std::filesystem::path outputDirectory{IIGENERALDOCUMENT_TEST_OUTPUT_DIR};
    std::filesystem::create_directories(outputDirectory);
    const auto createdPath = outputDirectory / "editable-elements.pdf";
    const auto editedPath = outputDirectory / "edited-elements.pdf";

    Document source;
    source.metadata()["Title"] = "Editable PDF elements";
    source.addPage(Page(Rect{0.0, 0.0, 320.0, 480.0}));
    DocumentEditor sourceEditor(source);
    sourceEditor.addText(0, "Original", Point{32.0, 420.0}, 20.0);
    sourceEditor.addRectangle(0, Rect{30.0, 330.0, 120.0, 50.0});

    constexpr std::array<std::byte, 12> pixels{
        std::byte{0xff}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0xff}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    sourceEditor.addRgbImage(
        0, std::span<const std::byte>(pixels), 2, 2,
        Matrix{80.0, 0.0, 0.0, 80.0, 180.0, 300.0});
    sourceEditor.addUnknown(0, {
        {{}, "BX", {}},
        {{PdfValue::integer(1)}, "IiFutureOperator", {}},
        {{}, "EX", {}},
    });

    Annotation note;
    note.setSubtype("/Text");
    note.setRect(Rect{280.0, 420.0, 20.0, 20.0});
    note.setContents("editable annotation");
    sourceEditor.addAnnotation(0, std::move(note));

    const auto created = PdfDocumentWriter{}.write(source, createdPath);
    expect(!created.hasErrors(), "a new PDF is written");
    expect(std::filesystem::file_size(createdPath) > 0, "created PDF is non-empty");

    auto read = PdfDocumentReader{}.read(createdPath);
    expect(!read.hasErrors(), "created PDF is readable");
    expect(read.document.pages().size() == 1, "page count round-trips");
    expect(hasKind(read.document, ElementKind::text), "text is recognized separately");
    expect(hasKind(read.document, ElementKind::path), "path is recognized separately");
    expect(hasKind(read.document, ElementKind::image), "image is recognized separately");
    expect(hasKind(read.document, ElementKind::unknown),
           "future operators are retained instead of discarded");
    expect(read.document.pages().front().annotations().size() == 1,
           "annotation is recognized separately");
    expect(firstText(read.document)->textSegments().front() == "Original",
           "text bytes round-trip");

    DocumentEditor readEditor(read.document);
    readEditor.replaceText(firstText(read.document)->id(), 0, "Modified");
    read.document.pages().front().annotations().front().setContents("modified annotation");
    const auto edited = PdfDocumentWriter{}.write(read.document, editedPath);
    expect(!edited.hasErrors(), "an existing PDF is rewritten");

    auto reread = PdfDocumentReader{}.read(editedPath);
    expect(firstText(reread.document)->textSegments().front() == "Modified",
           "one text object remains editable after reopening");
    expect(reread.document.pages().front().annotations().front().contents()
               == "modified annotation",
           "annotation edits persist");
}
