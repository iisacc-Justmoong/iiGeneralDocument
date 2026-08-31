#pragma once

#include "iiGeneralDocument/Export.h"

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ii::document {

enum class WordParagraphAlignment {
    automatic,
    left,
    center,
    right,
    justified,
};

struct IIGENERALDOCUMENT_EXPORT WordRunProperties {
    bool bold{false};
    bool italic{false};
    bool underline{false};
    std::string fontFamily;
    std::string eastAsiaFontFamily;
    double fontSizePoints{0.0};
    std::string color;
};

struct IIGENERALDOCUMENT_EXPORT WordRun {
    std::string text;
    WordRunProperties properties;
};

struct IIGENERALDOCUMENT_EXPORT WordParagraphProperties {
    std::string styleId;
    WordParagraphAlignment alignment{WordParagraphAlignment::automatic};
    std::optional<int> numberingId;
    int numberingLevel{0};
};

struct IIGENERALDOCUMENT_EXPORT WordParagraph {
    WordParagraphProperties properties;
    std::vector<WordRun> runs;

    [[nodiscard]] std::string plainText() const;
};

struct IIGENERALDOCUMENT_EXPORT WordTableCell {
    std::vector<WordParagraph> paragraphs;
};

struct IIGENERALDOCUMENT_EXPORT WordTableRow {
    std::vector<WordTableCell> cells;
};

struct IIGENERALDOCUMENT_EXPORT WordTable {
    std::vector<WordTableRow> rows;
};

using WordBlock = std::variant<WordParagraph, WordTable>;

struct IIGENERALDOCUMENT_EXPORT WordSectionProperties {
    int pageWidthTwips{12240};
    int pageHeightTwips{15840};
    int marginTopTwips{1440};
    int marginRightTwips{1440};
    int marginBottomTwips{1440};
    int marginLeftTwips{1440};
};

class IIGENERALDOCUMENT_EXPORT WordDocument {
public:
    [[nodiscard]] const std::vector<WordBlock>& blocks() const noexcept;
    [[nodiscard]] std::vector<WordBlock>& blocks() noexcept;
    void appendParagraph(WordParagraph paragraph);
    void appendTable(WordTable table);

    [[nodiscard]] const std::map<std::string, std::string>& metadata() const noexcept;
    [[nodiscard]] std::map<std::string, std::string>& metadata() noexcept;

    [[nodiscard]] const WordSectionProperties& section() const noexcept;
    [[nodiscard]] WordSectionProperties& section() noexcept;

    [[nodiscard]] std::string plainText() const;

private:
    std::vector<WordBlock> blocks_;
    std::map<std::string, std::string> metadata_;
    WordSectionProperties section_;
};

} // namespace ii::document
