#include "Editing/DocumentEditor.h"

#include "Core/Diagnostic.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace ii::document {
namespace {

Element* findInElements(
    std::vector<std::unique_ptr<Element>>& elements,
    ElementId id,
    std::unordered_set<const FormContent*>& visited)
{
    for (auto& element : elements) {
        if (element->id() == id) {
            return element.get();
        }
        auto* form = dynamic_cast<FormXObjectElement*>(element.get());
        if (!form || !form->content() || !visited.insert(form->content().get()).second) {
            continue;
        }
        if (auto* nested = findInElements(form->content()->elements(), id, visited)) {
            return nested;
        }
    }
    return nullptr;
}

bool removeFromElements(
    std::vector<std::unique_ptr<Element>>& elements,
    ElementId id,
    std::unordered_set<const FormContent*>& visited)
{
    const auto found = std::ranges::find_if(elements, [id](const auto& element) {
        return element->id() == id;
    });
    if (found != elements.end()) {
        elements.erase(found);
        return true;
    }
    for (auto& element : elements) {
        auto* form = dynamic_cast<FormXObjectElement*>(element.get());
        if (!form || !form->content() || !visited.insert(form->content().get()).second) {
            continue;
        }
        if (removeFromElements(form->content()->elements(), id, visited)) {
            return true;
        }
    }
    return false;
}

void updateMaximumId(
    const std::vector<std::unique_ptr<Element>>& elements,
    std::uint64_t& maximum,
    std::unordered_set<const FormContent*>& visited)
{
    for (const auto& element : elements) {
        maximum = std::max(maximum, element->id().value);
        const auto* form = dynamic_cast<const FormXObjectElement*>(element.get());
        if (!form || !form->content() || !visited.insert(form->content().get()).second) {
            continue;
        }
        updateMaximumId(form->content()->elements(), maximum, visited);
    }
}

} // namespace

DocumentEditor::DocumentEditor(Document& document) noexcept
    : document_(document)
{
}

Element* DocumentEditor::find(ElementId id) noexcept
{
    std::unordered_set<const FormContent*> visited;
    for (auto& page : document_.pages()) {
        if (auto* element = findInElements(page.elements(), id, visited)) {
            return element;
        }
    }
    return nullptr;
}

const Element* DocumentEditor::find(ElementId id) const noexcept
{
    return const_cast<DocumentEditor*>(this)->find(id);
}

ElementId DocumentEditor::addText(
    std::size_t pageIndex,
    std::string textBytes,
    Point origin,
    double fontSize,
    std::string fontResource)
{
    const auto id = nextElementId();
    page(pageIndex).append(TextElement::create(
        id, std::move(textBytes), origin, fontSize, std::move(fontResource)));
    return id;
}

ElementId DocumentEditor::addRectangle(std::size_t pageIndex, Rect rect)
{
    const auto id = nextElementId();
    page(pageIndex).append(PathElement::rectangle(id, rect));
    return id;
}

ElementId DocumentEditor::addRgbImage(
    std::size_t pageIndex,
    std::span<const std::byte> pixels,
    int width,
    int height,
    Matrix placement)
{
    const auto id = nextElementId();
    const std::string resourceName = "/IiImage" + std::to_string(id.value);
    page(pageIndex).append(ImageElement::createRgb(
        id, pixels, width, height, placement, resourceName));
    return id;
}

ElementId DocumentEditor::addUnknown(
    std::size_t pageIndex, std::vector<PdfInstruction> instructions)
{
    const auto id = nextElementId();
    page(pageIndex).append(std::make_unique<UnknownElement>(id, std::move(instructions)));
    return id;
}

void DocumentEditor::replaceText(
    ElementId id, std::size_t segmentIndex, std::string textBytes)
{
    auto* text = dynamic_cast<TextElement*>(find(id));
    if (!text) {
        throw DocumentError("Requested element is not editable text");
    }
    text->replaceTextSegment(segmentIndex, std::move(textBytes));
}

void DocumentEditor::replaceImage(ElementId id, ImageReplacement replacement)
{
    auto* image = dynamic_cast<ImageElement*>(find(id));
    if (!image) {
        throw DocumentError("Requested element is not an image");
    }
    image->replace(std::move(replacement));
}

bool DocumentEditor::remove(ElementId id)
{
    std::unordered_set<const FormContent*> visited;
    for (auto& page : document_.pages()) {
        if (removeFromElements(page.elements(), id, visited)) {
            return true;
        }
    }
    return false;
}

AnnotationId DocumentEditor::addAnnotation(std::size_t pageIndex, Annotation annotation)
{
    const auto id = nextAnnotationId();
    annotation.setId(id);
    page(pageIndex).addAnnotation(std::move(annotation));
    return id;
}

bool DocumentEditor::removeAnnotation(AnnotationId id)
{
    for (auto& page : document_.pages()) {
        if (page.removeAnnotation(id)) {
            return true;
        }
    }
    return false;
}

Page& DocumentEditor::page(std::size_t index)
{
    if (index >= document_.pages().size()) {
        throw DocumentError("Page index is out of range");
    }
    return document_.pages()[index];
}

ElementId DocumentEditor::nextElementId() const noexcept
{
    std::uint64_t maximum = 0;
    std::unordered_set<const FormContent*> visited;
    for (const auto& page : document_.pages()) {
        updateMaximumId(page.elements(), maximum, visited);
    }
    return ElementId{maximum + 1};
}

AnnotationId DocumentEditor::nextAnnotationId() const noexcept
{
    std::uint64_t maximum = 0;
    for (const auto& page : document_.pages()) {
        for (const auto& annotation : page.annotations()) {
            maximum = std::max(maximum, annotation.id().value);
        }
    }
    return AnnotationId{maximum + 1};
}

} // namespace ii::document
