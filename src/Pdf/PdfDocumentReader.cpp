#include "Pdf/PdfDocumentReader.h"

#include "Core/PdfValue.h"
#include "Model/Element.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ii::document {
namespace {

PdfValue toPdfValue(QPDFObjectHandle object, int depth = 0, bool expandIndirect = true)
{
    if (object.isIndirect() && depth > 0 && !expandIndirect) {
        const auto reference = object.getObjGen();
        return PdfValue::raw(std::to_string(reference.getObj()) + " "
                             + std::to_string(reference.getGen()) + " R");
    }
    if (depth > 32) {
        return PdfValue::raw(object.unparse());
    }
    if (object.isNull()) {
        return PdfValue::null();
    }
    if (object.isBool()) {
        return PdfValue::boolean(object.getBoolValue());
    }
    if (object.isInteger()) {
        return PdfValue::integer(object.getIntValue());
    }
    if (object.isReal()) {
        return PdfValue::real(object.getNumericValue(), object.getRealValue());
    }
    if (object.isName()) {
        return PdfValue::name(object.getName());
    }
    if (object.isString()) {
        return PdfValue::string(object.getStringValue());
    }
    if (object.isArray()) {
        PdfValue::Array values;
        values.reserve(static_cast<std::size_t>(object.getArrayNItems()));
        for (auto item : object.aitems()) {
            values.push_back(toPdfValue(item, depth + 1, false));
        }
        return PdfValue::array(std::move(values));
    }
    if (object.isDictionary()) {
        PdfValue::Dictionary values;
        for (auto [key, value] : object.ditems()) {
            values.emplace_back(key, toPdfValue(value, depth + 1, false));
        }
        return PdfValue::dictionary(std::move(values));
    }
    return PdfValue::raw(object.unparse());
}

std::optional<SourceReference> sourceReference(QPDFObjectHandle object)
{
    if (!object.isIndirect()) {
        return std::nullopt;
    }
    const auto reference = object.getObjGen();
    return SourceReference{reference.getObj(), reference.getGen()};
}

class ContentCollector final : public QPDFObjectHandle::ParserCallbacks {
public:
    void handleObject(QPDFObjectHandle object, std::size_t, std::size_t) override
    {
        if (object.isOperator()) {
            instructions.push_back(
                {std::exchange(operands, {}), object.getOperatorValue(), {}});
        } else if (object.isInlineImage()) {
            if (!operands.empty()) {
                instructions.push_back({std::exchange(operands, {}), {}, {}});
            }
            instructions.push_back({{}, {}, object.getInlineImageValue()});
        } else {
            operands.push_back(toPdfValue(object));
        }
    }

    void handleEOF() override
    {
        if (!operands.empty()) {
            instructions.push_back({std::exchange(operands, {}), {}, {}});
        }
    }

    std::vector<PdfInstruction> instructions;

private:
    std::vector<PdfValue> operands;
};

bool operatorIn(std::string_view value, std::initializer_list<std::string_view> choices)
{
    return std::ranges::find(choices, value) != choices.end();
}

bool isPathConstruction(const std::string& value)
{
    return operatorIn(value, {"m", "l", "c", "v", "y", "h", "re"});
}

bool isPathTerminator(const std::string& value)
{
    return operatorIn(value, {"S", "s", "f", "F", "f*", "B", "B*", "b", "b*", "n"});
}

bool isMarkedContent(const std::string& value)
{
    return operatorIn(value, {"MP", "DP", "BMC", "BDC", "EMC"});
}

bool isGraphicsState(const std::string& value)
{
    return operatorIn(value,
        {"q", "Q", "cm", "w", "J", "j", "M", "d", "ri", "i", "gs",
         "CS", "cs", "SC", "SCN", "sc", "scn", "G", "g", "RG", "rg", "K", "k"});
}

std::optional<std::string> nameOperand(const PdfInstruction& instruction)
{
    if (instruction.operands.empty()
        || instruction.operands.front().kind() != PdfValueKind::name) {
        return std::nullopt;
    }
    return instruction.operands.front().stringValue();
}

double numericOperand(const PdfInstruction& instruction, std::size_t index)
{
    if (index >= instruction.operands.size()) {
        return 0.0;
    }
    const auto& value = instruction.operands[index];
    if (value.kind() != PdfValueKind::integer && value.kind() != PdfValueKind::real) {
        return 0.0;
    }
    return value.realValue();
}

std::optional<Rect> rectangleBounds(const std::vector<PdfInstruction>& instructions)
{
    for (const auto& instruction : instructions) {
        if (instruction.operatorName == "re" && instruction.operands.size() >= 4) {
            return Rect{numericOperand(instruction, 0), numericOperand(instruction, 1),
                        numericOperand(instruction, 2), numericOperand(instruction, 3)};
        }
    }
    return std::nullopt;
}

using ImageMap = std::map<std::string, ImageInfo>;
struct FormResource {
    std::optional<SourceReference> source;
    std::shared_ptr<FormContent> content;
};
using FormMap = std::map<std::string, FormResource>;
using FormCache = std::map<SourceReference, std::shared_ptr<FormContent>>;

std::vector<std::unique_ptr<Element>> classify(
    std::vector<PdfInstruction> instructions,
    const ImageMap& images,
    const FormMap& forms,
    std::uint64_t& nextId)
{
    std::vector<std::unique_ptr<Element>> result;
    std::size_t index = 0;
    const auto makeId = [&nextId] { return ElementId{nextId++}; };

    while (index < instructions.size()) {
        const auto& instruction = instructions[index];
        const std::string operatorName = instruction.operatorName;

        if (instruction.operatorName == "BT") {
            std::size_t end = index + 1;
            while (end < instructions.size() && instructions[end].operatorName != "ET") {
                ++end;
            }
            if (end < instructions.size()) {
                ++end;
            }
            std::vector<PdfInstruction> group(
                std::make_move_iterator(instructions.begin() + static_cast<std::ptrdiff_t>(index)),
                std::make_move_iterator(instructions.begin() + static_cast<std::ptrdiff_t>(end)));
            result.push_back(std::make_unique<TextElement>(makeId(), std::move(group)));
            index = end;
            continue;
        }

        if (instruction.operatorName == "BI") {
            std::size_t end = index + 1;
            while (end < instructions.size() && !instructions[end].isInlineImageData()) {
                ++end;
            }
            if (end < instructions.size()) {
                ++end;
            }
            std::vector<PdfInstruction> group(
                std::make_move_iterator(instructions.begin() + static_cast<std::ptrdiff_t>(index)),
                std::make_move_iterator(instructions.begin() + static_cast<std::ptrdiff_t>(end)));
            result.push_back(std::make_unique<InlineImageElement>(makeId(), std::move(group)));
            index = end;
            continue;
        }

        if (isPathConstruction(instruction.operatorName)) {
            std::size_t end = index + 1;
            while (end < instructions.size()
                   && !isPathTerminator(instructions[end].operatorName)) {
                ++end;
            }
            if (end < instructions.size()) {
                ++end;
            }
            std::vector<PdfInstruction> group(
                std::make_move_iterator(instructions.begin() + static_cast<std::ptrdiff_t>(index)),
                std::make_move_iterator(instructions.begin() + static_cast<std::ptrdiff_t>(end)));
            auto path = std::make_unique<PathElement>(makeId(), std::move(group));
            path->setBounds(rectangleBounds(path->instructions()));
            result.push_back(std::move(path));
            index = end;
            continue;
        }

        if (instruction.operatorName == "q" && index + 3 < instructions.size()
            && instructions[index + 1].operatorName == "cm"
            && instructions[index + 2].operatorName == "Do"
            && instructions[index + 3].operatorName == "Q") {
            const auto resourceName = nameOperand(instructions[index + 2]);
            if (resourceName && (images.contains(*resourceName) || forms.contains(*resourceName))) {
                const std::size_t end = index + 4;
                std::vector<PdfInstruction> group(
                    std::make_move_iterator(instructions.begin() + static_cast<std::ptrdiff_t>(index)),
                    std::make_move_iterator(instructions.begin() + static_cast<std::ptrdiff_t>(end)));
                if (const auto image = images.find(*resourceName); image != images.end()) {
                    auto element = std::make_unique<ImageElement>(makeId(), std::move(group), image->second);
                    const auto& cm = element->instructions()[1];
                    element->setBounds(Matrix{
                        numericOperand(cm, 0), numericOperand(cm, 1), numericOperand(cm, 2),
                        numericOperand(cm, 3), numericOperand(cm, 4), numericOperand(cm, 5)}.mapUnitSquare());
                    result.push_back(std::move(element));
                } else {
                    const auto& form = forms.at(*resourceName);
                    result.push_back(std::make_unique<FormXObjectElement>(
                        makeId(), std::move(group), *resourceName,
                        form.source, form.content));
                }
                index = end;
                continue;
            }
        }

        if (instruction.operatorName == "Do") {
            const auto resourceName = nameOperand(instruction);
            std::vector<PdfInstruction> group;
            group.push_back(std::move(instructions[index]));
            if (resourceName) {
                if (const auto image = images.find(*resourceName); image != images.end()) {
                    result.push_back(std::make_unique<ImageElement>(makeId(), std::move(group), image->second));
                } else if (const auto form = forms.find(*resourceName); form != forms.end()) {
                    result.push_back(std::make_unique<FormXObjectElement>(
                        makeId(), std::move(group), *resourceName,
                        form->second.source, form->second.content));
                } else {
                    result.push_back(std::make_unique<UnknownElement>(makeId(), std::move(group)));
                }
            } else {
                result.push_back(std::make_unique<UnknownElement>(makeId(), std::move(group)));
            }
            ++index;
            continue;
        }

        std::vector<PdfInstruction> group;
        group.push_back(std::move(instructions[index]));
        if (operatorName == "sh") {
            result.push_back(std::make_unique<ShadingElement>(makeId(), std::move(group)));
        } else if (isMarkedContent(operatorName)) {
            result.push_back(std::make_unique<MarkedContentElement>(makeId(), std::move(group)));
        } else if (isGraphicsState(operatorName)) {
            result.push_back(std::make_unique<GraphicsStateElement>(makeId(), std::move(group)));
        } else {
            result.push_back(std::make_unique<UnknownElement>(makeId(), std::move(group)));
        }
        ++index;
    }
    return result;
}

ImageInfo imageInfo(const std::string& name, QPDFObjectHandle image)
{
    const auto dictionary = image.getDict();
    ImageInfo info;
    info.resourceName = name;
    if (const auto width = dictionary.getKey("/Width"); width.isInteger()) {
        info.width = width.getIntValueAsInt();
    }
    if (const auto height = dictionary.getKey("/Height"); height.isInteger()) {
        info.height = height.getIntValueAsInt();
    }
    if (const auto bits = dictionary.getKey("/BitsPerComponent"); bits.isInteger()) {
        info.bitsPerComponent = bits.getIntValueAsInt();
    }
    const auto colorSpace = dictionary.getKey("/ColorSpace");
    if (!colorSpace.isNull()) {
        info.colorSpace = colorSpace.isName() ? colorSpace.getName() : colorSpace.unparse();
    }
    const auto filter = dictionary.getKey("/Filter");
    if (!filter.isNull()) {
        info.filterSyntax = filter.unparse();
    }
    const auto decodeParameters = dictionary.getKey("/DecodeParms");
    if (!decodeParameters.isNull()) {
        info.decodeParametersSyntax = decodeParameters.unparse();
    }
    if (const auto bytes = image.getRawStreamData()) {
        info.encodedBytes.resize(bytes->getSize());
        std::transform(bytes->getBuffer(), bytes->getBuffer() + bytes->getSize(),
                       info.encodedBytes.begin(), [](unsigned char byte) {
                           return static_cast<std::byte>(byte);
                       });
    }
    info.source = sourceReference(image);
    return info;
}

FormResource readFormContent(
    QPDFObjectHandle form,
    FormCache& cache,
    std::set<SourceReference>& activeForms,
    std::uint64_t& nextElementId,
    std::size_t depth)
{
    const auto reference = sourceReference(form);
    if (reference) {
        if (const auto found = cache.find(*reference); found != cache.end()) {
            return FormResource{
                reference, activeForms.contains(*reference) ? nullptr : found->second};
        }
    }

    auto content = std::make_shared<FormContent>(reference);
    if (reference) {
        cache.emplace(*reference, content);
        activeForms.insert(*reference);
    }
    if (depth >= 32) {
        if (reference) {
            activeForms.erase(*reference);
        }
        return FormResource{reference, content};
    }

    QPDFPageObjectHelper helper(form);
    ImageMap images;
    for (auto [name, image] : helper.getImages()) {
        images.emplace(name, imageInfo(name, image));
    }
    FormMap forms;
    for (auto [name, nestedForm] : helper.getFormXObjects()) {
        forms.emplace(
            name, readFormContent(
                nestedForm, cache, activeForms, nextElementId, depth + 1));
    }
    ContentCollector collector;
    helper.parseContents(&collector);
    for (auto& element : classify(
             std::move(collector.instructions), images, forms, nextElementId)) {
        content->append(std::move(element));
    }
    if (reference) {
        activeForms.erase(*reference);
    }
    return FormResource{reference, content};
}

Rect pageMediaBox(QPDFPageObjectHelper& page)
{
    const auto rectangle = page.getMediaBox().getArrayAsRectangle();
    return Rect{rectangle.llx, rectangle.lly,
                rectangle.urx - rectangle.llx, rectangle.ury - rectangle.lly};
}

int pageRotation(QPDFPageObjectHelper& page)
{
    const auto value = page.getAttribute("/Rotate", false);
    return value.isInteger() ? value.getIntValueAsInt() : 0;
}

FormFieldType formFieldType(const std::string& pdfType)
{
    if (pdfType == "/Tx") {
        return FormFieldType::text;
    }
    if (pdfType == "/Btn") {
        return FormFieldType::button;
    }
    if (pdfType == "/Ch") {
        return FormFieldType::choice;
    }
    if (pdfType == "/Sig") {
        return FormFieldType::signature;
    }
    return FormFieldType::unknown;
}

std::vector<char> readFile(const std::filesystem::path& source)
{
    std::ifstream stream(source, std::ios::binary);
    if (!stream) {
        throw DocumentError("Unable to open PDF source: " + source.string());
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

ReadResult PdfDocumentReader::read(
    const std::filesystem::path& source, const ReadOptions& options) const
{
    ReadResult result;
    try {
        auto bytes = readFile(source);
        if (bytes.empty()) {
            throw DocumentError("PDF source is empty: " + source.string());
        }

        QPDF pdf;
        pdf.setAttemptRecovery(options.attemptRecovery);
        pdf.setSuppressWarnings(true);
        const char* password = options.password.empty() ? nullptr : options.password.c_str();
        pdf.processMemoryFile(source.string().c_str(), bytes.data(), bytes.size(), password);

        result.document.setPdfVersion(pdf.getPDFVersion());
        const bool encrypted = pdf.isEncrypted();
        bool digitallySigned = false;
        std::uint64_t nextElementId = 1;
        std::uint64_t nextAnnotationId = 1;
        std::uint64_t nextFieldId = 1;
        FormCache formCache;
        std::set<SourceReference> activeForms;

        const auto info = pdf.getTrailer().getKey("/Info");
        if (info.isDictionary()) {
            for (const std::string key : {"/Title", "/Author", "/Subject", "/Keywords",
                                          "/Creator", "/Producer", "/CreationDate", "/ModDate"}) {
                const auto value = info.getKey(key);
                if (value.isString()) {
                    result.document.metadata()[key.substr(1)] = value.getUTF8Value();
                }
            }
        }

        auto pages = QPDFPageDocumentHelper::get(pdf).getAllPages();
        for (std::size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
            auto& sourcePage = pages[pageIndex];
            Page page(pageMediaBox(sourcePage));
            page.setRotation(pageRotation(sourcePage));
            page.setSourcePageIndex(pageIndex);

            ImageMap images;
            for (auto [name, image] : sourcePage.getImages()) {
                images.emplace(name, imageInfo(name, image));
            }
            FormMap forms;
            for (auto [name, form] : sourcePage.getFormXObjects()) {
                forms.emplace(name, readFormContent(
                    form, formCache, activeForms, nextElementId, 0));
            }

            ContentCollector collector;
            sourcePage.parseContents(&collector);
            for (auto& element : classify(
                     std::move(collector.instructions), images, forms, nextElementId)) {
                page.append(std::move(element));
            }

            for (auto sourceAnnotation : sourcePage.getAnnotations()) {
                Annotation annotation;
                annotation.setId(AnnotationId{nextAnnotationId++});
                annotation.setSubtype(sourceAnnotation.getSubtype());
                const auto rectangle = sourceAnnotation.getRect();
                annotation.setRect(Rect{rectangle.llx, rectangle.lly,
                                        rectangle.urx - rectangle.llx,
                                        rectangle.ury - rectangle.lly});
                annotation.setFlags(sourceAnnotation.getFlags());
                const auto handle = sourceAnnotation.getObjectHandle();
                const auto contents = handle.getKey("/Contents");
                if (contents.isString()) {
                    annotation.setContents(contents.getUTF8Value());
                }
                annotation.setDictionary(toPdfValue(handle));
                annotation.setSource(sourceReference(handle));
                page.addAnnotation(std::move(annotation));
            }
            result.document.addPage(std::move(page));
        }

        QPDFAcroFormDocumentHelper formHelper(pdf);
        if (formHelper.hasAcroForm()) {
            for (auto sourceField : formHelper.getFormFields()) {
                FormField field;
                field.setId(FormFieldId{nextFieldId++});
                field.setName(sourceField.getFullyQualifiedName());
                field.setOriginalName(sourceField.getFullyQualifiedName());
                field.setType(formFieldType(sourceField.getFieldType()));
                const auto fieldValue = sourceField.getValue();
                field.setValue(toPdfValue(fieldValue));
                if ((field.type() == FormFieldType::text
                     || field.type() == FormFieldType::choice)
                    && fieldValue.isString()) {
                    field.setUtf8Value(fieldValue.getUTF8Value());
                }
                field.setFlags(sourceField.getFlags());
                field.setChoices(sourceField.getChoices());
                field.setSource(sourceReference(sourceField.getObjectHandle()));
                digitallySigned = digitallySigned || field.type() == FormFieldType::signature;
                result.document.addFormField(std::move(field));
            }
        }

        for (const auto& warning : pdf.getWarnings()) {
            result.diagnostics.push_back({DiagnosticSeverity::warning, "pdf.recovered",
                                          warning.what(), source.string()});
        }

        result.document.origin_ = Document::Origin{
            std::move(bytes), source.string(), options.password, encrypted, digitallySigned};
    } catch (const std::exception& error) {
        result.diagnostics.push_back(
            {DiagnosticSeverity::error, "pdf.read_failed", error.what(), source.string()});
    }
    return result;
}

} // namespace ii::document
