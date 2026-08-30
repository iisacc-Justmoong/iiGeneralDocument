#include "Validation/DocumentValidator.h"

#include <cmath>
#include <string>
#include <unordered_set>

namespace ii::document {
namespace {

void addError(
    std::vector<Diagnostic>& diagnostics,
    std::string code,
    std::string message,
    std::string context)
{
    diagnostics.push_back(
        {DiagnosticSeverity::error, std::move(code), std::move(message), std::move(context)});
}

bool containsOperator(const Element& element, const std::string& operatorName)
{
    for (const auto& instruction : element.instructions()) {
        if (instruction.operatorName == operatorName) {
            return true;
        }
    }
    return false;
}

void validateElements(
    const std::vector<std::unique_ptr<Element>>& elements,
    const std::string& ownerContext,
    std::vector<Diagnostic>& diagnostics,
    std::unordered_set<ElementId>& elementIds,
    std::unordered_set<const FormContent*>& visitedForms)
{
    for (const auto& element : elements) {
        const std::string elementContext = ownerContext + ", element "
            + std::to_string(element->id().value);
        if (element->id().value == 0 || !elementIds.insert(element->id()).second) {
            addError(diagnostics, "element.invalid_id",
                     "Element ids must be non-zero and unique within a document.",
                     elementContext);
        }
        if (element->instructions().empty()) {
            addError(diagnostics, "element.empty",
                     "Every page element must preserve at least one PDF instruction.",
                     elementContext);
        }
        if (element->kind() == ElementKind::text
            && (!containsOperator(*element, "BT") || !containsOperator(*element, "ET"))) {
            addError(diagnostics, "text.unbalanced_object",
                     "A text element must contain both BT and ET operators.", elementContext);
        }
        if (const auto* image = dynamic_cast<const ImageElement*>(element.get())) {
            if (image->info().resourceName.empty()) {
                addError(diagnostics, "image.missing_resource",
                         "An image occurrence requires a resource name.", elementContext);
            }
            if (const auto& replacement = image->replacement()) {
                if (replacement->width <= 0 || replacement->height <= 0
                    || replacement->bitsPerComponent <= 0) {
                    addError(diagnostics, "image.invalid_dimensions",
                             "Replacement image dimensions and bit depth must be positive.",
                             elementContext);
                }
                if (replacement->filterSyntax.empty()
                    && replacement->colorSpace == "/DeviceRGB"
                    && replacement->bitsPerComponent == 8
                    && replacement->bytes.size()
                        != static_cast<std::size_t>(replacement->width)
                            * static_cast<std::size_t>(replacement->height) * 3U) {
                    addError(diagnostics, "image.invalid_rgb_payload",
                             "An unfiltered 8-bit RGB image requires three bytes per pixel.",
                             elementContext);
                }
            }
        }
        const auto* form = dynamic_cast<const FormXObjectElement*>(element.get());
        if (form && form->content()
            && visitedForms.insert(form->content().get()).second) {
            validateElements(form->content()->elements(), elementContext + ", form content",
                             diagnostics, elementIds, visitedForms);
        }
    }
}

} // namespace

std::vector<Diagnostic> DocumentValidator::validate(const Document& document) const
{
    std::vector<Diagnostic> diagnostics;
    std::unordered_set<ElementId> elementIds;
    std::unordered_set<std::uint64_t> annotationIds;
    std::unordered_set<const FormContent*> visitedForms;

    for (std::size_t pageIndex = 0; pageIndex < document.pages().size(); ++pageIndex) {
        const auto& page = document.pages()[pageIndex];
        const std::string pageContext = "page " + std::to_string(pageIndex + 1);
        if (!page.mediaBox().isFinite() || page.mediaBox().width <= 0.0
            || page.mediaBox().height <= 0.0) {
            addError(diagnostics, "page.invalid_media_box",
                     "The page media box must have finite positive dimensions.", pageContext);
        }
        if (page.rotation() % 90 != 0) {
            addError(diagnostics, "page.invalid_rotation",
                     "PDF page rotation must be a multiple of 90 degrees.", pageContext);
        }

        validateElements(page.elements(), pageContext, diagnostics, elementIds, visitedForms);

        for (const auto& annotation : page.annotations()) {
            const std::string annotationContext = pageContext + ", annotation "
                + std::to_string(annotation.id().value);
            if (annotation.id().value == 0
                || !annotationIds.insert(annotation.id().value).second) {
                addError(diagnostics, "annotation.invalid_id",
                         "Annotation ids must be non-zero and unique within a document.",
                         annotationContext);
            }
            if (annotation.subtype().empty() || annotation.subtype().front() != '/') {
                addError(diagnostics, "annotation.invalid_subtype",
                         "Annotation subtype must be a canonical PDF name.", annotationContext);
            }
            if (!annotation.rect().isFinite()) {
                addError(diagnostics, "annotation.invalid_rect",
                         "Annotation bounds must be finite.", annotationContext);
            }
        }
    }

    std::unordered_set<std::uint64_t> fieldIds;
    for (const auto& field : document.formFields()) {
        const std::string context = "form field " + field.name();
        if (field.id().value == 0 || !fieldIds.insert(field.id().value).second) {
            addError(diagnostics, "form.invalid_id",
                     "Form field ids must be non-zero and unique within a document.", context);
        }
        if (field.name().empty()) {
            addError(diagnostics, "form.missing_name", "Form fields require a name.", context);
        }
    }

    return diagnostics;
}

bool DocumentValidator::hasErrors(const Document& document) const
{
    return ii::document::hasErrors(validate(document));
}

} // namespace ii::document
