#pragma once

#include "Core/PdfValue.h"
#include "Model/Geometry.h"
#include "iiGeneralDocument/Export.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ii::document {

class FormContent;

struct ElementId {
    std::uint64_t value{0};
    auto operator<=>(const ElementId&) const = default;
};

struct SourceReference {
    int objectNumber{0};
    int generation{0};
    auto operator<=>(const SourceReference&) const = default;
};

enum class ElementKind {
    text,
    path,
    image,
    formXObject,
    inlineImage,
    shading,
    markedContent,
    graphicsState,
    unknown,
};

class IIGENERALDOCUMENT_EXPORT Element {
public:
    virtual ~Element();

    [[nodiscard]] ElementId id() const noexcept;
    [[nodiscard]] virtual ElementKind kind() const noexcept = 0;
    [[nodiscard]] const std::vector<PdfInstruction>& instructions() const noexcept;
    [[nodiscard]] std::vector<PdfInstruction>& instructions() noexcept;
    [[nodiscard]] const std::optional<Rect>& bounds() const noexcept;
    void setBounds(std::optional<Rect> bounds);

protected:
    Element(ElementId id, std::vector<PdfInstruction> instructions);

private:
    ElementId id_;
    std::vector<PdfInstruction> instructions_;
    std::optional<Rect> bounds_;
};

class IIGENERALDOCUMENT_EXPORT TextElement final : public Element {
public:
    TextElement(ElementId id, std::vector<PdfInstruction> instructions);

    static std::unique_ptr<TextElement> create(
        ElementId id,
        std::string textBytes,
        Point origin,
        double fontSize,
        std::string fontResource = "/F1");

    [[nodiscard]] ElementKind kind() const noexcept override;
    [[nodiscard]] std::vector<std::string> textSegments() const;
    void replaceTextSegment(std::size_t index, std::string textBytes);
};

class IIGENERALDOCUMENT_EXPORT PathElement final : public Element {
public:
    PathElement(ElementId id, std::vector<PdfInstruction> instructions);

    static std::unique_ptr<PathElement> rectangle(ElementId id, Rect rect);

    [[nodiscard]] ElementKind kind() const noexcept override;
};

struct ImageInfo {
    std::string resourceName;
    int width{0};
    int height{0};
    int bitsPerComponent{0};
    std::string colorSpace;
    std::string filterSyntax;
    std::string decodeParametersSyntax;
    std::vector<std::byte> encodedBytes;
    std::optional<SourceReference> source;
};

struct ImageReplacement {
    std::vector<std::byte> bytes;
    int width{0};
    int height{0};
    int bitsPerComponent{8};
    std::string colorSpace{"/DeviceRGB"};
    std::string filterSyntax;
    std::string decodeParametersSyntax;
};

class IIGENERALDOCUMENT_EXPORT ImageElement final : public Element {
public:
    ImageElement(ElementId id, std::vector<PdfInstruction> instructions, ImageInfo info);

    static std::unique_ptr<ImageElement> createRgb(
        ElementId id,
        std::span<const std::byte> pixels,
        int width,
        int height,
        Matrix placement,
        std::string resourceName);

    [[nodiscard]] ElementKind kind() const noexcept override;
    [[nodiscard]] const ImageInfo& info() const noexcept;
    [[nodiscard]] const std::optional<ImageReplacement>& replacement() const noexcept;
    void replace(ImageReplacement replacement);

private:
    ImageInfo info_;
    std::optional<ImageReplacement> replacement_;
};

class IIGENERALDOCUMENT_EXPORT FormXObjectElement final : public Element {
public:
    FormXObjectElement(
        ElementId id,
        std::vector<PdfInstruction> instructions,
        std::string resourceName,
        std::optional<SourceReference> source = std::nullopt,
        std::shared_ptr<FormContent> content = {});

    [[nodiscard]] ElementKind kind() const noexcept override;
    [[nodiscard]] const std::string& resourceName() const noexcept;
    [[nodiscard]] const std::optional<SourceReference>& source() const noexcept;
    [[nodiscard]] const std::shared_ptr<FormContent>& content() const noexcept;

private:
    std::string resourceName_;
    std::optional<SourceReference> source_;
    std::shared_ptr<FormContent> content_;
};

class IIGENERALDOCUMENT_EXPORT InlineImageElement final : public Element {
public:
    InlineImageElement(ElementId id, std::vector<PdfInstruction> instructions);
    [[nodiscard]] ElementKind kind() const noexcept override;
};

class IIGENERALDOCUMENT_EXPORT ShadingElement final : public Element {
public:
    ShadingElement(ElementId id, std::vector<PdfInstruction> instructions);
    [[nodiscard]] ElementKind kind() const noexcept override;
};

class IIGENERALDOCUMENT_EXPORT MarkedContentElement final : public Element {
public:
    MarkedContentElement(ElementId id, std::vector<PdfInstruction> instructions);
    [[nodiscard]] ElementKind kind() const noexcept override;
};

class IIGENERALDOCUMENT_EXPORT GraphicsStateElement final : public Element {
public:
    GraphicsStateElement(ElementId id, std::vector<PdfInstruction> instructions);
    [[nodiscard]] ElementKind kind() const noexcept override;
};

class IIGENERALDOCUMENT_EXPORT UnknownElement final : public Element {
public:
    UnknownElement(ElementId id, std::vector<PdfInstruction> instructions);
    [[nodiscard]] ElementKind kind() const noexcept override;
};

class IIGENERALDOCUMENT_EXPORT FormContent {
public:
    explicit FormContent(std::optional<SourceReference> source = std::nullopt);
    FormContent(FormContent&&) noexcept;
    FormContent& operator=(FormContent&&) noexcept;
    ~FormContent();
    FormContent(const FormContent&) = delete;
    FormContent& operator=(const FormContent&) = delete;

    [[nodiscard]] const std::optional<SourceReference>& source() const noexcept;
    [[nodiscard]] const std::vector<std::unique_ptr<Element>>& elements() const noexcept;
    [[nodiscard]] std::vector<std::unique_ptr<Element>>& elements() noexcept;
    void append(std::unique_ptr<Element> element);

private:
    std::optional<SourceReference> source_;
    std::vector<std::unique_ptr<Element>> elements_;
};

} // namespace ii::document

template<>
struct std::hash<ii::document::ElementId> {
    std::size_t operator()(ii::document::ElementId id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
