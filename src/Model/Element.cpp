#include "Model/Element.h"

#include "Core/Diagnostic.h"

#include <functional>
#include <utility>

namespace ii::document {
namespace {

void collectStringValues(const PdfValue& value, std::vector<std::string>& result)
{
    if (value.kind() == PdfValueKind::string) {
        result.push_back(value.stringValue());
    } else if (value.kind() == PdfValueKind::array) {
        for (const auto& child : value.arrayItems()) {
            if (child.kind() == PdfValueKind::string) {
                result.push_back(child.stringValue());
            }
        }
    }
}

bool replaceStringValue(PdfValue& value, std::size_t& cursor, std::size_t target,
                        const std::string& replacement)
{
    if (value.kind() == PdfValueKind::string) {
        if (cursor == target) {
            value.stringValue() = replacement;
            return true;
        }
        ++cursor;
        return false;
    }
    if (value.kind() == PdfValueKind::array) {
        for (auto& child : value.arrayItems()) {
            if (child.kind() == PdfValueKind::string) {
                if (replaceStringValue(child, cursor, target, replacement)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool isTextShowingOperator(const std::string& operatorName)
{
    return operatorName == "Tj" || operatorName == "TJ" || operatorName == "'"
        || operatorName == "\"";
}

PdfInstruction numericInstruction(std::initializer_list<double> values, std::string operatorName)
{
    PdfInstruction instruction;
    instruction.operatorName = std::move(operatorName);
    for (const double value : values) {
        instruction.operands.push_back(PdfValue::real(value));
    }
    return instruction;
}

} // namespace

Element::Element(ElementId id, std::vector<PdfInstruction> instructions)
    : id_(id)
    , instructions_(std::move(instructions))
{
}

Element::~Element() = default;

ElementId Element::id() const noexcept
{
    return id_;
}

const std::vector<PdfInstruction>& Element::instructions() const noexcept
{
    return instructions_;
}

std::vector<PdfInstruction>& Element::instructions() noexcept
{
    return instructions_;
}

const std::optional<Rect>& Element::bounds() const noexcept
{
    return bounds_;
}

void Element::setBounds(std::optional<Rect> bounds)
{
    bounds_ = std::move(bounds);
}

TextElement::TextElement(ElementId id, std::vector<PdfInstruction> instructions)
    : Element(id, std::move(instructions))
{
}

std::unique_ptr<TextElement> TextElement::create(
    ElementId id,
    std::string textBytes,
    Point origin,
    double fontSize,
    std::string fontResource)
{
    std::vector<PdfInstruction> instructions;
    instructions.push_back({{}, "BT", {}});
    instructions.push_back({{PdfValue::name(std::move(fontResource)), PdfValue::real(fontSize)},
                            "Tf", {}});
    instructions.push_back(numericInstruction({origin.x, origin.y}, "Td"));
    instructions.push_back({{PdfValue::string(std::move(textBytes))}, "Tj", {}});
    instructions.push_back({{}, "ET", {}});
    return std::make_unique<TextElement>(id, std::move(instructions));
}

ElementKind TextElement::kind() const noexcept
{
    return ElementKind::text;
}

std::vector<std::string> TextElement::textSegments() const
{
    std::vector<std::string> result;
    for (const auto& instruction : instructions()) {
        if (!isTextShowingOperator(instruction.operatorName)) {
            continue;
        }
        for (const auto& operand : instruction.operands) {
            collectStringValues(operand, result);
        }
    }
    return result;
}

void TextElement::replaceTextSegment(std::size_t index, std::string textBytes)
{
    std::size_t cursor = 0;
    for (auto& instruction : instructions()) {
        if (!isTextShowingOperator(instruction.operatorName)) {
            continue;
        }
        for (auto& operand : instruction.operands) {
            if (replaceStringValue(operand, cursor, index, textBytes)) {
                return;
            }
        }
    }
    throw DocumentError("Text segment index is out of range");
}

PathElement::PathElement(ElementId id, std::vector<PdfInstruction> instructions)
    : Element(id, std::move(instructions))
{
}

std::unique_ptr<PathElement> PathElement::rectangle(ElementId id, Rect rect)
{
    std::vector<PdfInstruction> instructions;
    instructions.push_back(numericInstruction({rect.x, rect.y, rect.width, rect.height}, "re"));
    instructions.push_back({{}, "S", {}});
    auto result = std::make_unique<PathElement>(id, std::move(instructions));
    result->setBounds(rect);
    return result;
}

ElementKind PathElement::kind() const noexcept
{
    return ElementKind::path;
}

ImageElement::ImageElement(
    ElementId id, std::vector<PdfInstruction> instructions, ImageInfo info)
    : Element(id, std::move(instructions))
    , info_(std::move(info))
{
}

std::unique_ptr<ImageElement> ImageElement::createRgb(
    ElementId id,
    std::span<const std::byte> pixels,
    int width,
    int height,
    Matrix placement,
    std::string resourceName)
{
    ImageInfo info{resourceName, width, height, 8, "/DeviceRGB", {}, {}, {}, std::nullopt};
    std::vector<PdfInstruction> instructions;
    instructions.push_back({{}, "q", {}});
    instructions.push_back(numericInstruction(
        {placement.a, placement.b, placement.c, placement.d, placement.e, placement.f}, "cm"));
    instructions.push_back({{PdfValue::name(resourceName)}, "Do", {}});
    instructions.push_back({{}, "Q", {}});
    auto result = std::make_unique<ImageElement>(id, std::move(instructions), std::move(info));
    result->replace(ImageReplacement{
        std::vector<std::byte>(pixels.begin(), pixels.end()), width, height, 8,
        "/DeviceRGB", {}, {}});
    result->setBounds(placement.mapUnitSquare());
    return result;
}

ElementKind ImageElement::kind() const noexcept
{
    return ElementKind::image;
}

const ImageInfo& ImageElement::info() const noexcept
{
    return info_;
}

const std::optional<ImageReplacement>& ImageElement::replacement() const noexcept
{
    return replacement_;
}

void ImageElement::replace(ImageReplacement replacement)
{
    replacement_ = std::move(replacement);
}

FormXObjectElement::FormXObjectElement(
    ElementId id,
    std::vector<PdfInstruction> instructions,
    std::string resourceName,
    std::optional<SourceReference> source,
    std::shared_ptr<FormContent> content)
    : Element(id, std::move(instructions))
    , resourceName_(std::move(resourceName))
    , source_(source)
    , content_(std::move(content))
{
}

ElementKind FormXObjectElement::kind() const noexcept
{
    return ElementKind::formXObject;
}

const std::string& FormXObjectElement::resourceName() const noexcept
{
    return resourceName_;
}

const std::optional<SourceReference>& FormXObjectElement::source() const noexcept
{
    return source_;
}

const std::shared_ptr<FormContent>& FormXObjectElement::content() const noexcept
{
    return content_;
}

InlineImageElement::InlineImageElement(ElementId id, std::vector<PdfInstruction> instructions)
    : Element(id, std::move(instructions))
{
}

ElementKind InlineImageElement::kind() const noexcept
{
    return ElementKind::inlineImage;
}

ShadingElement::ShadingElement(ElementId id, std::vector<PdfInstruction> instructions)
    : Element(id, std::move(instructions))
{
}

ElementKind ShadingElement::kind() const noexcept
{
    return ElementKind::shading;
}

MarkedContentElement::MarkedContentElement(
    ElementId id, std::vector<PdfInstruction> instructions)
    : Element(id, std::move(instructions))
{
}

ElementKind MarkedContentElement::kind() const noexcept
{
    return ElementKind::markedContent;
}

GraphicsStateElement::GraphicsStateElement(
    ElementId id, std::vector<PdfInstruction> instructions)
    : Element(id, std::move(instructions))
{
}

ElementKind GraphicsStateElement::kind() const noexcept
{
    return ElementKind::graphicsState;
}

UnknownElement::UnknownElement(ElementId id, std::vector<PdfInstruction> instructions)
    : Element(id, std::move(instructions))
{
}

ElementKind UnknownElement::kind() const noexcept
{
    return ElementKind::unknown;
}

FormContent::FormContent(std::optional<SourceReference> source)
    : source_(source)
{
}

FormContent::FormContent(FormContent&&) noexcept = default;
FormContent& FormContent::operator=(FormContent&&) noexcept = default;
FormContent::~FormContent() = default;

const std::optional<SourceReference>& FormContent::source() const noexcept
{
    return source_;
}

const std::vector<std::unique_ptr<Element>>& FormContent::elements() const noexcept
{
    return elements_;
}

std::vector<std::unique_ptr<Element>>& FormContent::elements() noexcept
{
    return elements_;
}

void FormContent::append(std::unique_ptr<Element> element)
{
    if (!element) {
        throw DocumentError("Cannot append a null form element");
    }
    elements_.push_back(std::move(element));
}

} // namespace ii::document
