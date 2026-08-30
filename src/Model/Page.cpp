#include "Model/Page.h"

#include "Core/Diagnostic.h"

#include <algorithm>
#include <utility>

namespace ii::document {

AnnotationId Annotation::id() const noexcept
{
    return id_;
}

void Annotation::setId(AnnotationId id) noexcept
{
    id_ = id;
}

const std::string& Annotation::subtype() const noexcept
{
    return subtype_;
}

void Annotation::setSubtype(std::string subtype)
{
    subtype_ = std::move(subtype);
}

const Rect& Annotation::rect() const noexcept
{
    return rect_;
}

void Annotation::setRect(Rect rect) noexcept
{
    rect_ = rect;
}

const std::string& Annotation::contents() const noexcept
{
    return contents_;
}

void Annotation::setContents(std::string utf8Contents)
{
    contents_ = std::move(utf8Contents);
}

int Annotation::flags() const noexcept
{
    return flags_;
}

void Annotation::setFlags(int flags) noexcept
{
    flags_ = flags;
}

const PdfValue& Annotation::dictionary() const noexcept
{
    return dictionary_;
}

PdfValue& Annotation::dictionary() noexcept
{
    return dictionary_;
}

void Annotation::setDictionary(PdfValue dictionary)
{
    if (dictionary.kind() != PdfValueKind::dictionary) {
        throw DocumentError("Annotation data must be a PDF dictionary");
    }
    dictionary_ = std::move(dictionary);
}

const std::optional<SourceReference>& Annotation::source() const noexcept
{
    return source_;
}

void Annotation::setSource(std::optional<SourceReference> source) noexcept
{
    source_ = source;
}

Page::Page(Rect mediaBox)
    : mediaBox_(mediaBox)
{
}

Page::Page(Page&&) noexcept = default;
Page& Page::operator=(Page&&) noexcept = default;
Page::~Page() = default;

const Rect& Page::mediaBox() const noexcept
{
    return mediaBox_;
}

void Page::setMediaBox(Rect mediaBox) noexcept
{
    mediaBox_ = mediaBox;
}

int Page::rotation() const noexcept
{
    return rotation_;
}

void Page::setRotation(int degrees) noexcept
{
    rotation_ = degrees;
}

const std::vector<std::unique_ptr<Element>>& Page::elements() const noexcept
{
    return elements_;
}

std::vector<std::unique_ptr<Element>>& Page::elements() noexcept
{
    return elements_;
}

void Page::append(std::unique_ptr<Element> element)
{
    if (!element) {
        throw DocumentError("Cannot append a null page element");
    }
    elements_.push_back(std::move(element));
}

Element* Page::find(ElementId id) noexcept
{
    const auto found = std::ranges::find_if(elements_, [id](const auto& element) {
        return element->id() == id;
    });
    return found == elements_.end() ? nullptr : found->get();
}

const Element* Page::find(ElementId id) const noexcept
{
    return const_cast<Page*>(this)->find(id);
}

bool Page::remove(ElementId id)
{
    const auto previousSize = elements_.size();
    std::erase_if(elements_, [id](const auto& element) { return element->id() == id; });
    return elements_.size() != previousSize;
}

const std::vector<Annotation>& Page::annotations() const noexcept
{
    return annotations_;
}

std::vector<Annotation>& Page::annotations() noexcept
{
    return annotations_;
}

void Page::addAnnotation(Annotation annotation)
{
    annotations_.push_back(std::move(annotation));
}

Annotation* Page::findAnnotation(AnnotationId id) noexcept
{
    const auto found = std::ranges::find_if(annotations_, [id](const Annotation& annotation) {
        return annotation.id() == id;
    });
    return found == annotations_.end() ? nullptr : &*found;
}

const Annotation* Page::findAnnotation(AnnotationId id) const noexcept
{
    return const_cast<Page*>(this)->findAnnotation(id);
}

bool Page::removeAnnotation(AnnotationId id)
{
    const auto previousSize = annotations_.size();
    std::erase_if(annotations_, [id](const Annotation& annotation) {
        return annotation.id() == id;
    });
    return annotations_.size() != previousSize;
}

const std::optional<std::size_t>& Page::sourcePageIndex() const noexcept
{
    return sourcePageIndex_;
}

void Page::setSourcePageIndex(std::optional<std::size_t> index) noexcept
{
    sourcePageIndex_ = index;
}

} // namespace ii::document
