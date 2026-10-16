#include "Editing/DocumentEditor.h"

#include "Core/Diagnostic.h"
#include "Metadata/Authorship_p.hpp"
#include "Pdf/PdfDocumentReader.h"
#include "Pdf/PdfDocumentWriter.h"
#include "Pdf/Private/TextValidation.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>
#include <utility>

namespace ii::document {
namespace {
void requireMutable(const Document& document) {
    if (document.hasDigitalSignatures()) throw DocumentError("Signed PDFs cannot be manipulated");
    if (document.isEncrypted()) throw DocumentError("Encrypted PDFs cannot be manipulated");
    if (document.hasXfa()) throw DocumentError("XFA PDFs cannot be manipulated");
}
void requireText(std::string_view value) {
    if (!detail::validPdfText(value)) throw DocumentError("Text must be valid UTF-8 without NUL and at most 1 MiB");
}
pdf::Document nativeDocument(const Document& document) {
    requireMutable(document);
    auto encoded = PdfDocumentWriter{}.writeBytes(document);
    if (encoded.hasErrors()) {
        const auto error = std::ranges::find_if(encoded.diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == DiagnosticSeverity::error;
        });
        throw DocumentError(error->message);
    }
    auto parsed = pdf::Document::fromBytes(encoded.bytes);
    if (!parsed) throw DocumentError(parsed.error().message);
    return std::move(*parsed);
}
Document modeledDocument(pdf::Document document) {
    const auto bytes = document.toBytes();
    auto result = PdfDocumentReader{}.readBytes(bytes, "assembled PDF", ReadOptions{.attemptRecovery = false});
    if (result.hasErrors()) throw DocumentError(result.diagnostics.back().message);
    return std::move(result.document);
}


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
    document_.recordChange();
    return id;
}

ElementId DocumentEditor::addRectangle(std::size_t pageIndex, Rect rect)
{
    const auto id = nextElementId();
    page(pageIndex).append(PathElement::rectangle(id, rect));
    document_.recordChange();
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
    document_.recordChange();
    return id;
}

ElementId DocumentEditor::addUnknown(
    std::size_t pageIndex, std::vector<PdfInstruction> instructions)
{
    const auto id = nextElementId();
    page(pageIndex).append(std::make_unique<UnknownElement>(id, std::move(instructions)));
    document_.recordChange();
    return id;
}

void DocumentEditor::replaceText(
    ElementId id, std::size_t segmentIndex, std::string textBytes)
{
    auto* text = dynamic_cast<TextElement*>(find(id));
    if (!text) {
        throw DocumentError("Requested element is not editable text");
    }
    const auto segments = text->textSegments();
    if (segmentIndex < segments.size() && segments[segmentIndex] == textBytes) return;
    text->replaceTextSegment(segmentIndex, std::move(textBytes));
    document_.recordChange();
}

void DocumentEditor::replaceImage(ElementId id, ImageReplacement replacement)
{
    auto* image = dynamic_cast<ImageElement*>(find(id));
    if (!image) {
        throw DocumentError("Requested element is not an image");
    }
    if (image->replacement() && *image->replacement() == replacement) return;
    image->replace(std::move(replacement));
    document_.recordChange();
}

bool DocumentEditor::remove(ElementId id)
{
    std::unordered_set<const FormContent*> visited;
    for (auto& page : document_.pages()) {
        if (removeFromElements(page.elements(), id, visited)) {
            document_.recordChange();
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
    document_.recordChange();
    return id;
}

bool DocumentEditor::removeAnnotation(AnnotationId id)
{
    for (auto& page : document_.pages()) {
        if (page.removeAnnotation(id)) {
            document_.recordChange();
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


Document DocumentEditor::extractPages(std::span<const std::size_t> selected) const {
    auto native = nativeDocument(document_);
    auto extracted = native.extractPages(selected);
    if (!extracted) throw DocumentError(extracted.error().message);
    auto result = modeledDocument(std::move(*extracted));
    result.authorship_ = document_.authorship_;
    result.recordChange();
    return result;
}

Document DocumentEditor::mergePages(std::span<const DocumentPageSelection> selections) {
    if (selections.empty()) throw DocumentError("At least one document selection is required");
    std::vector<pdf::Document> documents;
    documents.reserve(selections.size());
    for (const auto& item : selections) documents.push_back(nativeDocument(item.document.get()));
    std::vector<pdf::PageSelection> nativeSelections;
    nativeSelections.reserve(selections.size());
    for (std::size_t i = 0; i < selections.size(); ++i)
        nativeSelections.push_back({std::cref(documents[i]), selections[i].pages});
    auto merged = pdf::Document::merge(nativeSelections);
    if (!merged) throw DocumentError(merged.error().message);
    auto result = modeledDocument(std::move(*merged));
    result.authorship_ = selections.front().document.get().authorship_;
    result.recordChange();
    return result;
}

void DocumentEditor::rotatePages(std::span<const std::size_t> selected, int degrees) {
    requireMutable(document_);
    if (selected.empty() || degrees % 90 != 0) throw DocumentError("Select pages and a multiple of 90 degrees");
    std::set<std::size_t> seen;
    std::vector<int> rotations;
    for (const auto index : selected) {
        if (index >= document_.pages().size() || !seen.insert(index).second)
            throw DocumentError("Rotation page indices must exist and be unique");
        const auto before = document_.pages()[index].rotation();
        if (before % 90) throw DocumentError("Source page rotation must be a multiple of 90 degrees");
        rotations.push_back(((before % 360 + degrees % 360) % 360 + 360) % 360);
    }
    if (degrees % 360 == 0) return;
    // Prepare authorship before changing pages, so rejected metadata cannot leave half a batch.
    auto authorship = document_.authorship_;
    auto metadata = document_.metadata_;
    authorship.recordChange(); detail::storeAuthorship(authorship, metadata);
    for (std::size_t i = 0; i < selected.size(); ++i) document_.pages()[selected[i]].setRotation(rotations[i]);
    document_.metadata_.swap(metadata); document_.authorship_ = std::move(authorship);
}

AnnotationId DocumentEditor::addTextNote(std::size_t index, Rect rectangle, std::string contents, std::string author) {
    requireMutable(document_); requireText(contents); requireText(author);
    if (contents.empty() || !rectangle.isFinite() || rectangle.width <= 0 || rectangle.height <= 0
        || !std::isfinite(rectangle.x + rectangle.width) || !std::isfinite(rectangle.y + rectangle.height))
        throw DocumentError("Notes require nonempty text and a finite positive rectangle");
    Annotation annotation;
    annotation.setSubtype("/Text"); annotation.setRect(rectangle);
    annotation.setContents(std::move(contents)); annotation.setAuthor(std::move(author));
    annotation.setDictionary(PdfValue::dictionary({{"/Name", PdfValue::name("/Note")}, {"/Open", PdfValue::boolean(false)}}));
    auto& target = page(index);
    const auto id = nextAnnotationId(); annotation.setId(id);
    auto authorship = document_.authorship_; auto metadata = document_.metadata_;
    authorship.recordChange(); detail::storeAuthorship(authorship, metadata);
    target.addAnnotation(std::move(annotation));
    document_.metadata_.swap(metadata); document_.authorship_ = std::move(authorship);
    return id;
}

bool DocumentEditor::setTextField(std::string_view name, std::string value) {
    requireMutable(document_); requireText(name); requireText(value);
    if (name.empty()) throw DocumentError("A field name is required");
    FormField* match{};
    for (auto& field : document_.formFields()) {
        if (field.name() == name) {
            if (match) throw DocumentError("Ambiguous field name");
            match = &field;
        }
    }
    if (!match) throw DocumentError("No existing field matches the name");
    if (match->type() != FormFieldType::text || !match->source())
        throw DocumentError("Only existing PDF text fields are supported");
    if ((match->flags() & 1) || (match->flags() & (1 << 25)) || match->maximumLength())
        throw DocumentError("Read-only, rich-text and MaxLen-constrained fields cannot be updated");
    if (match->utf8Value() && *match->utf8Value() == value) return false;
    auto authorship = document_.authorship_; auto metadata = document_.metadata_;
    authorship.recordChange(); detail::storeAuthorship(authorship, metadata);
    match->setUtf8Value(std::move(value));
    document_.metadata_.swap(metadata); document_.authorship_ = std::move(authorship);
    return true;
}

bool DocumentEditor::setMetadataBatch(std::span<const pdf::MetadataChange> changes) {
    requireMutable(document_);
    if (changes.empty()) throw DocumentError("At least one metadata change is required");
    const std::set<std::string> allowed{"Title", "Author", "Subject", "Keywords", "Creator", "Producer"};
    std::set<std::string> keys; auto metadata = document_.metadata_;
    for (const auto& change : changes) {
        if (!allowed.contains(change.key) || !keys.insert(change.key).second)
            throw DocumentError("Metadata keys must be supported and unique");
        if (change.value) { requireText(*change.value); metadata[change.key] = *change.value; }
        else metadata.erase(change.key);
    }
    if (metadata == document_.metadata_) return false;
    auto authorship = document_.authorship_; authorship.recordChange(); detail::storeAuthorship(authorship, metadata);
    document_.metadata_.swap(metadata); document_.authorship_ = std::move(authorship);
    return true;
}

} // namespace ii::document
