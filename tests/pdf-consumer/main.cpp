#include <iiGeneralDocument/Pdf/Objects.h>
#include "../PdfFixture.h"
#include <stdexcept>

namespace pdf = ii::document::pdf;
int main() {
    const auto raw = pdfFixture();
    auto loaded = pdf::Document::fromBytes(std::as_bytes(std::span(raw)));
    if (!loaded) throw std::runtime_error(loaded.error().message);
    pdf::Editor editor(*loaded);
    const std::size_t page[]{1};
    if (!editor.rotatePages(page, 90) || loaded->pages()[1].clockwiseRotation != 0) return 1;
    if (!editor.setTextField("customer", "installed consumer 📄")) return 2;
    auto selected = loaded->extractPages(page);
    if (!selected || selected->pages().size() != 1) return 3;
    const pdf::PageSelection selections[]{{std::cref(*loaded), {0}}, {std::cref(*selected), {0}}};
    auto merged = pdf::Document::merge(selections);
    if (!merged || merged->pages().size() != 2) return 4;
    auto reopened = pdf::Document::fromBytes(merged->toBytes());
    return reopened && reopened->formFields().size() == 2 ? 0 : 5;
}
