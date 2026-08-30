#pragma once

#include "Model/Document.h"
#include "iiGeneralDocument/Export.h"

#include <cstddef>
#include <span>
#include <string>

namespace ii::document {

class IIGENERALDOCUMENT_EXPORT DocumentEditor {
public:
    explicit DocumentEditor(Document& document) noexcept;

    [[nodiscard]] Element* find(ElementId id) noexcept;
    [[nodiscard]] const Element* find(ElementId id) const noexcept;

    ElementId addText(
        std::size_t pageIndex,
        std::string textBytes,
        Point origin,
        double fontSize,
        std::string fontResource = "/F1");
    ElementId addRectangle(std::size_t pageIndex, Rect rect);
    ElementId addRgbImage(
        std::size_t pageIndex,
        std::span<const std::byte> pixels,
        int width,
        int height,
        Matrix placement);
    ElementId addUnknown(std::size_t pageIndex, std::vector<PdfInstruction> instructions);

    void replaceText(ElementId id, std::size_t segmentIndex, std::string textBytes);
    void replaceImage(ElementId id, ImageReplacement replacement);
    bool remove(ElementId id);

    AnnotationId addAnnotation(std::size_t pageIndex, Annotation annotation);
    bool removeAnnotation(AnnotationId id);

private:
    [[nodiscard]] Page& page(std::size_t index);
    [[nodiscard]] ElementId nextElementId() const noexcept;
    [[nodiscard]] AnnotationId nextAnnotationId() const noexcept;

    Document& document_;
};

} // namespace ii::document
