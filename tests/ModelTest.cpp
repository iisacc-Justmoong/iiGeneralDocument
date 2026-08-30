#include "Core/PdfValue.h"
#include "Model/Document.h"
#include "Model/Element.h"
#include "Model/Page.h"
#include "TestSupport.h"

#include <memory>
#include <string>
#include <vector>

using namespace ii::document;

int main()
{
    expect(PdfValue::name("/A B").toPdfSyntax() == "/A#20B", "names are escaped");
    expect(PdfValue::string("a(b)\\c").toPdfSyntax() == "(a\\(b\\)\\\\c)",
           "literal strings are escaped");
    expect(PdfValue::array({PdfValue::integer(1), PdfValue::real(2.5)}).toPdfSyntax()
               == "[1 2.5]",
           "arrays serialize recursively");
    expect(PdfValue::real(1.0e20).toPdfSyntax() == "100000000000000000000",
           "real values never use exponent syntax forbidden by PDF");
    expect(PdfValue::real(1.0e-7).toPdfSyntax() == "0.0000001",
           "small real values remain valid decimal PDF tokens");

    auto text = TextElement::create(ElementId{1}, "Hello", Point{72.0, 700.0}, 18.0);
    expect(text->kind() == ElementKind::text, "text has a dedicated kind");
    expect(text->textSegments() == std::vector<std::string>{"Hello"},
           "text strings remain individually addressable");
    text->replaceTextSegment(0, "Editable");
    expect(text->textSegments().front() == "Editable", "one text segment can be edited");

    auto path = PathElement::rectangle(ElementId{2}, Rect{10.0, 20.0, 30.0, 40.0});
    expect(path->kind() == ElementKind::path, "paths are independent elements");
    expect(path->instructions().back().operatorName == "S", "rectangle has a paint operator");

    Page page(Rect{0.0, 0.0, 612.0, 792.0});
    page.append(std::move(text));
    page.append(std::move(path));
    expect(page.elements().size() == 2, "page owns ordered elements");
    expect(page.find(ElementId{2}) != nullptr, "elements are addressable by stable id");
    expect(page.remove(ElementId{1}), "page removes a selected element only");
    expect(page.elements().size() == 1, "unrelated elements remain");

    FormField field;
    field.setUtf8Value("editable form value");
    expect(field.utf8Value() && *field.utf8Value() == "editable form value",
           "form text has an explicit Unicode editing path");
}
