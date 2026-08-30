#include "Pdf/PdfDocumentWriter.h"

#include "Validation/DocumentValidator.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ii::document {
namespace {

using ResourceOverrides = std::unordered_map<ElementId, std::string>;

QPDFObjectHandle parseValue(QPDF& pdf, const PdfValue& value)
{
    return QPDFObjectHandle::parse(&pdf, value.toPdfSyntax(), "iiGeneralDocument value");
}

QPDFObjectHandle parseOptionalSyntax(QPDF& pdf, const std::string& syntax)
{
    return syntax.empty()
        ? QPDFObjectHandle::newNull()
        : QPDFObjectHandle::parse(&pdf, syntax, "iiGeneralDocument stream parameter");
}

std::string bytesAsString(const std::vector<std::byte>& bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

QPDFObjectHandle createImage(QPDF& pdf, const ImageReplacement& replacement)
{
    QPDFObjectHandle image = pdf.newStream();
    auto dictionary = QPDFObjectHandle::newDictionary();
    dictionary.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
    dictionary.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
    dictionary.replaceKey("/Width", QPDFObjectHandle::newInteger(replacement.width));
    dictionary.replaceKey("/Height", QPDFObjectHandle::newInteger(replacement.height));
    dictionary.replaceKey(
        "/BitsPerComponent", QPDFObjectHandle::newInteger(replacement.bitsPerComponent));
    dictionary.replaceKey(
        "/ColorSpace", QPDFObjectHandle::parse(&pdf, replacement.colorSpace, "image color space"));
    image.replaceDict(dictionary);
    image.replaceStreamData(bytesAsString(replacement.bytes),
                            parseOptionalSyntax(pdf, replacement.filterSyntax),
                            parseOptionalSyntax(pdf, replacement.decodeParametersSyntax));
    return image;
}

std::set<std::string> fontNames(
    const std::vector<std::unique_ptr<Element>>& elements)
{
    std::set<std::string> names;
    for (const auto& element : elements) {
        for (const auto& instruction : element->instructions()) {
            if (instruction.operatorName == "Tf" && !instruction.operands.empty()
                && instruction.operands.front().kind() == PdfValueKind::name) {
                names.insert(instruction.operands.front().stringValue());
            }
        }
    }
    return names;
}

QPDFObjectHandle ensureSubdictionary(
    QPDFObjectHandle dictionary, const std::string& key)
{
    auto child = dictionary.getKey(key);
    if (!child.isDictionary()) {
        child = QPDFObjectHandle::newDictionary();
        dictionary.replaceKey(key, child);
    }
    return child;
}

void ensureFonts(
    QPDF& pdf,
    QPDFObjectHandle resources,
    const std::vector<std::unique_ptr<Element>>& elements)
{
    auto fonts = ensureSubdictionary(resources, "/Font");
    for (const auto& name : fontNames(elements)) {
        if (fonts.hasKey(name)) {
            continue;
        }
        auto font = QPDFObjectHandle::newDictionary();
        font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
        font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
        font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
        font.replaceKey("/Encoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
        fonts.replaceKey(name, pdf.makeIndirectObject(font));
    }
}

std::string uniqueResourceName(
    QPDFObjectHandle xobjects, const ImageElement& image, bool forceNew)
{
    std::string candidate = image.info().resourceName;
    if (candidate.empty() || candidate.front() != '/') {
        candidate = "/IiImage" + std::to_string(image.id().value);
    }
    if (forceNew || xobjects.hasKey(candidate)) {
        candidate = "/IiEdited" + std::to_string(image.id().value);
    }
    std::size_t suffix = 1;
    const std::string base = candidate;
    while (xobjects.hasKey(candidate)) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

ResourceOverrides installImages(
    QPDF& pdf,
    QPDFObjectHandle resources,
    const std::vector<std::unique_ptr<Element>>& elements,
    bool sourceDocument)
{
    ResourceOverrides overrides;
    auto xobjects = ensureSubdictionary(resources, "/XObject");
    for (const auto& element : elements) {
        const auto* image = dynamic_cast<const ImageElement*>(element.get());
        if (!image || !image->replacement()) {
            continue;
        }
        const std::string resourceName = uniqueResourceName(xobjects, *image, sourceDocument);
        xobjects.replaceKey(resourceName, createImage(pdf, *image->replacement()));
        if (resourceName != image->info().resourceName) {
            overrides.emplace(image->id(), resourceName);
        }
    }
    return overrides;
}

std::string serializeElement(const Element& element, const ResourceOverrides& overrides)
{
    std::string result;
    for (const auto& instruction : element.instructions()) {
        if (instruction.operatorName == "Do") {
            if (const auto replacement = overrides.find(element.id()); replacement != overrides.end()) {
                PdfInstruction copy = instruction;
                if (copy.operands.empty()) {
                    copy.operands.push_back(PdfValue::name(replacement->second));
                } else {
                    copy.operands.front() = PdfValue::name(replacement->second);
                }
                result += copy.toPdfSyntax();
                continue;
            }
        }
        result += instruction.toPdfSyntax();
    }
    return result;
}

std::string serializeElements(
    const std::vector<std::unique_ptr<Element>>& elements,
    const ResourceOverrides& overrides)
{
    std::string result;
    for (const auto& element : elements) {
        result += serializeElement(*element, overrides);
    }
    return result;
}

QPDFObjectHandle pageResources(QPDFPageObjectHelper& page)
{
    auto resources = page.getAttribute("/Resources", true);
    if (!resources.isDictionary()) {
        resources = QPDFObjectHandle::newDictionary();
        page.getObjectHandle().replaceKey("/Resources", resources);
    }
    return resources;
}

void syncFormContents(
    QPDF& pdf,
    const std::vector<std::unique_ptr<Element>>& elements,
    std::set<SourceReference>& synchronized)
{
    for (const auto& element : elements) {
        const auto* form = dynamic_cast<const FormXObjectElement*>(element.get());
        if (!form || !form->content()) {
            continue;
        }
        const auto& content = *form->content();
        if (content.source() && !synchronized.insert(*content.source()).second) {
            continue;
        }
        syncFormContents(pdf, content.elements(), synchronized);
        if (!content.source()) {
            continue;
        }

        auto stream = pdf.getObject(
            content.source()->objectNumber, content.source()->generation);
        if (!stream.isStream()) {
            throw DocumentError("A modeled Form XObject no longer resolves to a stream");
        }
        QPDFPageObjectHelper helper(stream);
        auto resources = pageResources(helper);
        ensureFonts(pdf, resources, content.elements());
        const auto overrides = installImages(pdf, resources, content.elements(), true);
        stream.replaceStreamData(
            serializeElements(content.elements(), overrides),
            QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
    }
}

void setPageGeometry(QPDFPageObjectHelper& target, const Page& source)
{
    const auto& box = source.mediaBox();
    target.getObjectHandle().replaceKey(
        "/MediaBox",
        QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle{
            box.x, box.y, box.x + box.width, box.y + box.height}));
    if (source.rotation() == 0) {
        target.getObjectHandle().removeKey("/Rotate");
    } else {
        target.getObjectHandle().replaceKey(
            "/Rotate", QPDFObjectHandle::newInteger(source.rotation()));
    }
}

void applyDictionary(QPDF& pdf, QPDFObjectHandle target, const PdfValue& value)
{
    if (value.kind() != PdfValueKind::dictionary) {
        return;
    }
    std::set<std::string> retained;
    for (const auto& [key, child] : value.dictionaryItems()) {
        retained.insert(key);
        target.replaceKey(key, parseValue(pdf, child));
    }
    for (const auto& key : target.getKeys()) {
        if (!retained.contains(key)) {
            target.removeKey(key);
        }
    }
}

void applyAnnotation(QPDF& pdf, QPDFObjectHandle target, const Annotation& annotation)
{
    applyDictionary(pdf, target, annotation.dictionary());
    target.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    target.replaceKey("/Subtype", QPDFObjectHandle::newName(annotation.subtype()));
    const auto& rect = annotation.rect();
    target.replaceKey(
        "/Rect",
        QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle{
            rect.x, rect.y, rect.x + rect.width, rect.y + rect.height}));
    target.replaceKey("/F", QPDFObjectHandle::newInteger(annotation.flags()));
    if (annotation.contents().empty()) {
        target.removeKey("/Contents");
    } else {
        target.replaceKey(
            "/Contents", QPDFObjectHandle::newUnicodeString(annotation.contents()));
    }
}

void syncAnnotations(
    QPDF& pdf, QPDFPageObjectHelper& targetPage, const Page& sourcePage)
{
    const auto existingAnnotations = targetPage.getAnnotations();
    std::vector<QPDFObjectHandle> output;
    output.reserve(sourcePage.annotations().size());
    for (std::size_t index = 0; index < sourcePage.annotations().size(); ++index) {
        const auto& annotation = sourcePage.annotations()[index];
        QPDFObjectHandle target;
        if (annotation.source()) {
            target = pdf.getObject(
                annotation.source()->objectNumber, annotation.source()->generation);
        } else if (index < existingAnnotations.size()) {
            target = existingAnnotations[index].getObjectHandle();
        } else {
            target = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        }
        applyAnnotation(pdf, target, annotation);
        output.push_back(target);
    }
    if (output.empty()) {
        targetPage.getObjectHandle().removeKey("/Annots");
    } else {
        targetPage.getObjectHandle().replaceKey(
            "/Annots", QPDFObjectHandle::newArray(output));
    }
}

QPDFPageObjectHelper makePage(QPDF& pdf, QPDFPageDocumentHelper& pages, const Page& source)
{
    auto pageDictionary = QPDFObjectHandle::newDictionary();
    pageDictionary.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
    auto pageObject = pdf.makeIndirectObject(pageDictionary);
    QPDFPageObjectHelper target(pageObject);
    setPageGeometry(target, source);
    pageObject.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
    pages.addPage(target, false);
    return target;
}

void syncPage(
    QPDF& pdf,
    QPDFPageObjectHelper& target,
    const Page& source,
    bool sourceDocument,
    std::set<SourceReference>& synchronizedForms)
{
    setPageGeometry(target, source);
    syncFormContents(pdf, source.elements(), synchronizedForms);
    auto resources = pageResources(target);
    ensureFonts(pdf, resources, source.elements());
    const auto overrides = installImages(pdf, resources, source.elements(), sourceDocument);
    target.getObjectHandle().replaceKey(
        "/Contents", pdf.newStream(serializeElements(source.elements(), overrides)));
    syncAnnotations(pdf, target, source);
}

void syncMetadata(QPDF& pdf, const Document& document)
{
    auto trailer = pdf.getTrailer();
    auto info = trailer.getKey("/Info");
    if (!info.isDictionary()) {
        info = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        trailer.replaceKey("/Info", info);
    }
    for (const std::string key : {"Title", "Author", "Subject", "Keywords", "Creator",
                                  "Producer", "CreationDate", "ModDate"}) {
        const auto value = document.metadata().find(key);
        const std::string pdfKey = "/" + key;
        if (value == document.metadata().end()) {
            info.removeKey(pdfKey);
        } else {
            info.replaceKey(pdfKey, QPDFObjectHandle::newUnicodeString(value->second));
        }
    }
}

bool stringIsAscii(const std::string& value)
{
    return std::ranges::all_of(value, [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 32 && byte <= 126;
    });
}

void syncFormFields(QPDF& pdf, const Document& document, WriteResult& result)
{
    if (document.formFields().empty()) {
        return;
    }
    QPDFAcroFormDocumentHelper forms(pdf);
    if (!forms.hasAcroForm()) {
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "pdf.new_form_unsupported",
            "Form fields require an existing AcroForm and widget structure.",
            {}});
        return;
    }
    auto existing = forms.getFormFields();
    for (std::size_t index = 0; index < document.formFields().size(); ++index) {
        const auto& source = document.formFields()[index];
        auto found = existing.end();
        if (source.source()) {
            found = std::ranges::find_if(existing, [&](const auto& candidate) {
                const auto reference = candidate.getObjectHandle().getObjGen();
                return reference.getObj() == source.source()->objectNumber
                    && reference.getGen() == source.source()->generation;
            });
        }
        if (found == existing.end()) {
            for (auto candidate = existing.begin(); candidate != existing.end(); ++candidate) {
                if (candidate->getFullyQualifiedName() == source.originalName()) {
                    found = candidate;
                    break;
                }
            }
        }
        if (found == existing.end()) {
            result.diagnostics.push_back({DiagnosticSeverity::error, "pdf.form_field_missing",
                                          "The source form field no longer exists.", source.name()});
            continue;
        }

        auto field = *found;
        if (field.getFullyQualifiedName() != source.name()) {
            forms.setFormFieldName(field, source.name());
        }
        if (source.type() == FormFieldType::signature) {
            continue;
        }

        const bool variableText = source.type() == FormFieldType::text
            || source.type() == FormFieldType::choice;
        const auto value = source.utf8Value()
            ? QPDFObjectHandle::newUnicodeString(*source.utf8Value())
            : parseValue(pdf, source.value());
        const bool asciiAppearance = source.utf8Value()
            ? stringIsAscii(*source.utf8Value())
            : (source.value().kind() == PdfValueKind::string
               && stringIsAscii(source.value().stringValue()));
        if (variableText && !asciiAppearance) {
            field.setV(value, true);
            result.diagnostics.push_back({
                DiagnosticSeverity::warning,
                "pdf.form_appearance_deferred",
                "The logical form value was updated, but qpdf cannot generate a non-ASCII appearance.",
                source.name()});
        } else {
            field.setV(value, false);
            if (variableText) {
                for (auto annotation : forms.getAnnotationsForField(field)) {
                    field.generateAppearance(annotation);
                    if (annotation.getAppearanceStream("/N").isNull()) {
                        result.diagnostics.push_back({
                            DiagnosticSeverity::warning,
                            "pdf.form_appearance_missing",
                            "The form field has no normal appearance stream after generation.",
                            source.name()});
                    }
                }
            }
        }
    }
}

void verifyWrittenPdf(
    const std::filesystem::path& destination, std::size_t expectedPages, WriteResult& result)
{
    QPDF verification;
    verification.setSuppressWarnings(true);
    const std::string filename = destination.string();
    verification.processFile(filename.c_str());
    const auto pages = QPDFPageDocumentHelper::get(verification).getAllPages();
    if (pages.size() != expectedPages) {
        throw DocumentError("Written PDF page count does not match the document model");
    }
    for (auto page : pages) {
        class VerifyCallbacks final : public QPDFObjectHandle::ParserCallbacks {
        public:
            void handleObject(QPDFObjectHandle) override { ++objects; }
            void handleEOF() override {}
            std::size_t objects{0};
        } callbacks;
        page.parseContents(&callbacks);
    }
    for (const auto& warning : verification.getWarnings()) {
        result.diagnostics.push_back({DiagnosticSeverity::warning, "pdf.write_warning",
                                      warning.what(), destination.string()});
    }
}

} // namespace

WriteResult PdfDocumentWriter::write(
    const Document& document,
    const std::filesystem::path& destination,
    const WriteOptions& options) const
{
    WriteResult result;
    result.diagnostics = DocumentValidator{}.validate(document);
    if (result.hasErrors()) {
        return result;
    }
    if (document.hasDigitalSignatures() && !options.allowInvalidatingDigitalSignatures) {
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "pdf.signed_document_rejected",
            "Writing would invalidate at least one digital signature; opt in explicitly to continue.",
            destination.string()});
        return result;
    }
    if (document.isEncrypted() && !options.allowRemovingEncryption) {
        result.diagnostics.push_back({
            DiagnosticSeverity::error,
            "pdf.encrypted_document_rejected",
            "The current writer emits an unencrypted result; opt in explicitly to remove encryption.",
            destination.string()});
        return result;
    }

    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        if (document.origin_) {
            const auto& origin = *document.origin_;
            const char* password = origin.password.empty() ? nullptr : origin.password.c_str();
            pdf.processMemoryFile(origin.description.c_str(), origin.bytes.data(),
                                  origin.bytes.size(), password);
        } else {
            pdf.emptyPDF();
        }

        QPDFPageDocumentHelper pageDocument(pdf);
        auto targetPages = pageDocument.getAllPages();
        std::set<SourceReference> synchronizedForms;
        if (document.origin_) {
            if (targetPages.size() != document.pages().size()) {
                throw DocumentError(
                    "Adding, removing, or reordering source PDF pages is not supported yet");
            }
            for (std::size_t index = 0; index < document.pages().size(); ++index) {
                if (!document.pages()[index].sourcePageIndex()
                    || *document.pages()[index].sourcePageIndex() != index) {
                    throw DocumentError(
                        "Adding, removing, or reordering source PDF pages is not supported yet");
                }
                syncPage(pdf, targetPages[index], document.pages()[index], true,
                         synchronizedForms);
            }
        } else {
            for (const auto& sourcePage : document.pages()) {
                auto target = makePage(pdf, pageDocument, sourcePage);
                syncPage(pdf, target, sourcePage, false, synchronizedForms);
            }
        }

        syncMetadata(pdf, document);
        syncFormFields(pdf, document, result);
        if (result.hasErrors()) {
            return result;
        }

        const std::string filename = destination.string();
        QPDFWriter writer(pdf, filename.c_str());
        writer.setLinearization(options.linearize);
        writer.write();
        verifyWrittenPdf(destination, document.pages().size(), result);
    } catch (const std::exception& error) {
        result.diagnostics.push_back({DiagnosticSeverity::error, "pdf.write_failed",
                                      error.what(), destination.string()});
    }
    return result;
}

} // namespace ii::document
