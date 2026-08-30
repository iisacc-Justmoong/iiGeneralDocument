#include "Editing/DocumentEditor.h"
#include "Model/Document.h"
#include "TestSupport.h"

#include <array>
#include <cstddef>
#include <span>
#include <string>

using namespace ii::document;

int main()
{
    Document document;
    document.addPage(Page(Rect{0.0, 0.0, 300.0, 400.0}));
    DocumentEditor editor(document);

    const auto textId = editor.addText(0, "Before", Point{20.0, 350.0}, 12.0);
    const auto pathId = editor.addRectangle(0, Rect{10.0, 10.0, 50.0, 25.0});
    expect(textId != pathId, "editor allocates unique ids");

    editor.replaceText(textId, 0, "After");
    const auto* text = dynamic_cast<const TextElement*>(editor.find(textId));
    expect(text != nullptr && text->textSegments().front() == "After",
           "editor targets the requested text object");

    constexpr std::array<std::byte, 3> redPixel{
        std::byte{0xff}, std::byte{0x00}, std::byte{0x00}};
    const auto imageId = editor.addRgbImage(
        0, std::span<const std::byte>(redPixel), 1, 1, Matrix{40.0, 0.0, 0.0, 40.0, 80.0, 100.0});
    const auto* image = dynamic_cast<const ImageElement*>(editor.find(imageId));
    expect(image != nullptr && image->replacement().has_value(),
           "image payload is an independently editable resource");

    Annotation annotation;
    annotation.setSubtype("/Text");
    annotation.setRect(Rect{20.0, 20.0, 24.0, 24.0});
    annotation.setContents("review note");
    const auto annotationId = editor.addAnnotation(0, std::move(annotation));
    expect(document.pages().front().findAnnotation(annotationId) != nullptr,
           "annotations are separate from painted page contents");

    expect(editor.remove(pathId), "editor removes one element");
    expect(editor.find(textId) != nullptr && editor.find(imageId) != nullptr,
           "other objects survive removal");

    auto sharedForm = std::make_shared<FormContent>();
    sharedForm->append(TextElement::create(
        ElementId{51}, "Nested", Point{0.0, 0.0}, 9.0));
    document.pages().front().append(std::make_unique<FormXObjectElement>(
        ElementId{50},
        std::vector<PdfInstruction>{{{PdfValue::name("/SharedForm")}, "Do", {}}},
        "/SharedForm", std::nullopt, sharedForm));
    document.pages().front().append(std::make_unique<FormXObjectElement>(
        ElementId{52},
        std::vector<PdfInstruction>{{{PdfValue::name("/SharedForm")}, "Do", {}}},
        "/SharedForm", std::nullopt, sharedForm));
    editor.replaceText(ElementId{51}, 0, "Nested edit");
    const auto* nested = dynamic_cast<const TextElement*>(editor.find(ElementId{51}));
    expect(nested != nullptr && nested->textSegments().front() == "Nested edit",
           "editor traverses shared Form XObject contents exactly once");
}
