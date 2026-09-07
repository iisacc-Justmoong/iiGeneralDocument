#pragma once

#include "iiGeneralDocument/Export.h"
#include <iiFileProvider.h>

#include <map>
#include <functional>
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
    bool operator==(const WordRunProperties&) const = default;
    bool bold{false};
    bool italic{false};
    bool underline{false};
    std::string fontFamily;
    std::string eastAsiaFontFamily;
    double fontSizePoints{0.0};
    std::string color;
};

struct IIGENERALDOCUMENT_EXPORT WordRun {
    bool operator==(const WordRun&) const = default;
    std::string text;
    WordRunProperties properties;
};

struct IIGENERALDOCUMENT_EXPORT WordParagraphProperties {
    bool operator==(const WordParagraphProperties&) const = default;
    std::string styleId;
    WordParagraphAlignment alignment{WordParagraphAlignment::automatic};
    std::optional<int> numberingId;
    int numberingLevel{0};
    bool numberingContinuation{false};
};

struct IIGENERALDOCUMENT_EXPORT WordParagraph {
    bool operator==(const WordParagraph&) const = default;
    WordParagraphProperties properties;
    std::vector<WordRun> runs;

    [[nodiscard]] std::string plainText() const;
};

struct IIGENERALDOCUMENT_EXPORT WordTableCell {
    bool operator==(const WordTableCell&) const = default;
    std::vector<WordParagraph> paragraphs;
};

struct IIGENERALDOCUMENT_EXPORT WordTableRow {
    bool operator==(const WordTableRow&) const = default;
    std::vector<WordTableCell> cells;
};

struct IIGENERALDOCUMENT_EXPORT WordTable {
    bool operator==(const WordTable&) const = default;
    std::vector<WordTableRow> rows;
};

using WordBlock = std::variant<WordParagraph, WordTable>;

struct IIGENERALDOCUMENT_EXPORT WordSectionProperties {
    bool operator==(const WordSectionProperties&) const = default;
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
    bool edit(const std::function<bool(WordDocument&)>& callback);
    void appendParagraph(WordParagraph paragraph);
    void appendTable(WordTable table);

    [[nodiscard]] const iiFileProvider::Authorship& authorship() const noexcept;
    bool setFileAuthor(const iiFileProvider::FileAuthor& author);
    bool setMetadata(std::string key, std::string value);
    // For legacy direct aggregate edits, call once after a successful change.
    void recordChange();
    // Reader boundary: validates stored metadata and clears the active identity.
    void restoreAuthorship();

    [[nodiscard]] const std::map<std::string, std::string>& metadata() const noexcept;
    [[nodiscard]] std::map<std::string, std::string>& metadata() noexcept;

    [[nodiscard]] const WordSectionProperties& section() const noexcept;
    [[nodiscard]] WordSectionProperties& section() noexcept;

    [[nodiscard]] std::string plainText() const;

private:
    std::vector<WordBlock> blocks_;
    std::map<std::string, std::string> metadata_;
    iiFileProvider::Authorship authorship_;
    WordSectionProperties section_;
};

} // namespace ii::document
