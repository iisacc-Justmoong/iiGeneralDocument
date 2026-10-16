#pragma once

#include "Model/Document.h"
#include "iiGeneralDocument/Export.h"

#include <cstddef>
#include <span>
#include <string>

namespace ii::document {

struct DocumentPageSelection {
    std::reference_wrapper<const Document> document;
    std::vector<std::size_t> pages; // Empty selects all; zero based, ordered, repeated.
};

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

    [[nodiscard]] Document extractPages(std::span<const std::size_t> pages) const;
    [[nodiscard]] static Document mergePages(std::span<const DocumentPageSelection> selections);
    void rotatePages(std::span<const std::size_t> pages, int clockwiseDegrees);
    AnnotationId addTextNote(std::size_t pageIndex, Rect rectangle,
        std::string contents, std::string author = {});
    bool setTextField(std::string_view name, std::string value);
    bool setMetadataBatch(std::span<const pdf::MetadataChange> changes);

private:
    [[nodiscard]] Page& page(std::size_t index);
    [[nodiscard]] ElementId nextElementId() const noexcept;
    [[nodiscard]] AnnotationId nextAnnotationId() const noexcept;

    Document& document_;
};

} // namespace ii::document
