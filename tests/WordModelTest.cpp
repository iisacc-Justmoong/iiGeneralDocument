#include "TestSupport.h"
#include "Word/WordDocument.h"

#include <string>
#include <utility>
#include <variant>

using namespace ii::document;

int main()
{
    WordDocument document;
    document.metadata()["Title"] = "Editable Word document";

    WordParagraph heading;
    heading.properties.styleId = "Heading1";
    heading.properties.alignment = WordParagraphAlignment::center;
    heading.runs.push_back({"Editable", {.bold = true, .fontFamily = "Arial",
                                         .eastAsiaFontFamily = "Nanum Gothic",
                                         .fontSizePoints = 16.0, .color = "1F4E78"}});
    heading.runs.push_back({" Word"});
    expect(heading.plainText() == "Editable Word", "paragraph text joins editable runs");
    document.appendParagraph(std::move(heading));

    WordTable table;
    WordTableRow row;
    WordTableCell cell;
    WordParagraph cellParagraph;
    cellParagraph.runs.push_back({"Cell value"});
    cell.paragraphs.push_back(std::move(cellParagraph));
    row.cells.push_back(std::move(cell));
    table.rows.push_back(std::move(row));
    document.appendTable(std::move(table));

    expect(document.blocks().size() == 2, "word blocks retain document order");
    expect(std::holds_alternative<WordParagraph>(document.blocks().front()),
           "paragraphs remain independently addressable");
    expect(std::holds_alternative<WordTable>(document.blocks().back()),
           "tables remain independently addressable");
    expect(document.plainText().find("Cell value") != std::string::npos,
           "plain text includes table-cell paragraphs");
    expect(document.section().pageWidthTwips == 12240
               && document.section().pageHeightTwips == 15840,
           "new word documents use an explicit letter page geometry");
}
