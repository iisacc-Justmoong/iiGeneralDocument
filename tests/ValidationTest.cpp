#include "Model/Document.h"
#include "Model/Element.h"
#include "TestSupport.h"
#include "Validation/DocumentValidator.h"

#include <memory>

using namespace ii::document;

int main()
{
    Document valid;
    Page page(Rect{0.0, 0.0, 200.0, 300.0});
    page.append(TextElement::create(ElementId{1}, "valid", Point{10.0, 20.0}, 10.0));
    valid.addPage(std::move(page));
    expect(!DocumentValidator{}.hasErrors(valid), "valid document passes validation");

    Document invalid;
    Page invalidPage(Rect{0.0, 0.0, -1.0, 0.0});
    invalidPage.setRotation(45);
    invalidPage.append(TextElement::create(ElementId{7}, "one", Point{}, 10.0));
    invalidPage.append(TextElement::create(ElementId{7}, "two", Point{}, 10.0));
    invalid.addPage(std::move(invalidPage));
    const auto diagnostics = DocumentValidator{}.validate(invalid);
    expect(diagnostics.size() >= 3, "validator reports independent invariant failures");
}
