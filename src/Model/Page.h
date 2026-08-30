#pragma once

#include "Core/PdfValue.h"
#include "Model/Element.h"
#include "Model/Geometry.h"
#include "iiGeneralDocument/Export.h"

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ii::document {

struct AnnotationId {
    std::uint64_t value{0};
    auto operator<=>(const AnnotationId&) const = default;
};

class IIGENERALDOCUMENT_EXPORT Annotation {
public:
    [[nodiscard]] AnnotationId id() const noexcept;
    void setId(AnnotationId id) noexcept;
    [[nodiscard]] const std::string& subtype() const noexcept;
    void setSubtype(std::string subtype);
    [[nodiscard]] const Rect& rect() const noexcept;
    void setRect(Rect rect) noexcept;
    [[nodiscard]] const std::string& contents() const noexcept;
    void setContents(std::string utf8Contents);
    [[nodiscard]] int flags() const noexcept;
    void setFlags(int flags) noexcept;
    [[nodiscard]] const PdfValue& dictionary() const noexcept;
    [[nodiscard]] PdfValue& dictionary() noexcept;
    void setDictionary(PdfValue dictionary);
    [[nodiscard]] const std::optional<SourceReference>& source() const noexcept;
    void setSource(std::optional<SourceReference> source) noexcept;

private:
    AnnotationId id_;
    std::string subtype_;
    Rect rect_;
    std::string contents_;
    int flags_{0};
    PdfValue dictionary_{PdfValue::dictionary({})};
    std::optional<SourceReference> source_;
};

class IIGENERALDOCUMENT_EXPORT Page {
public:
    explicit Page(Rect mediaBox = Rect{0.0, 0.0, 612.0, 792.0});
    Page(Page&&) noexcept;
    Page& operator=(Page&&) noexcept;
    ~Page();
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    [[nodiscard]] const Rect& mediaBox() const noexcept;
    void setMediaBox(Rect mediaBox) noexcept;
    [[nodiscard]] int rotation() const noexcept;
    void setRotation(int degrees) noexcept;

    [[nodiscard]] const std::vector<std::unique_ptr<Element>>& elements() const noexcept;
    [[nodiscard]] std::vector<std::unique_ptr<Element>>& elements() noexcept;
    void append(std::unique_ptr<Element> element);
    [[nodiscard]] Element* find(ElementId id) noexcept;
    [[nodiscard]] const Element* find(ElementId id) const noexcept;
    bool remove(ElementId id);

    [[nodiscard]] const std::vector<Annotation>& annotations() const noexcept;
    [[nodiscard]] std::vector<Annotation>& annotations() noexcept;
    void addAnnotation(Annotation annotation);
    [[nodiscard]] Annotation* findAnnotation(AnnotationId id) noexcept;
    [[nodiscard]] const Annotation* findAnnotation(AnnotationId id) const noexcept;
    bool removeAnnotation(AnnotationId id);

    [[nodiscard]] const std::optional<std::size_t>& sourcePageIndex() const noexcept;
    void setSourcePageIndex(std::optional<std::size_t> index) noexcept;

private:
    Rect mediaBox_;
    int rotation_{0};
    std::vector<std::unique_ptr<Element>> elements_;
    std::vector<Annotation> annotations_;
    std::optional<std::size_t> sourcePageIndex_;
};

} // namespace ii::document
