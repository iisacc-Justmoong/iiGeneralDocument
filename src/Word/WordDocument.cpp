#include "Word/WordDocument.h"

#include <utility>

namespace ii::document {

std::string WordParagraph::plainText() const
{
    std::string result;
    for (const auto& run : runs) {
        result += run.text;
    }
    return result;
}

const std::vector<WordBlock>& WordDocument::blocks() const noexcept
{
    return blocks_;
}

std::vector<WordBlock>& WordDocument::blocks() noexcept
{
    return blocks_;
}

void WordDocument::appendParagraph(WordParagraph paragraph)
{
    blocks_.emplace_back(std::move(paragraph));
}

void WordDocument::appendTable(WordTable table)
{
    blocks_.emplace_back(std::move(table));
}

const std::map<std::string, std::string>& WordDocument::metadata() const noexcept
{
    return metadata_;
}

std::map<std::string, std::string>& WordDocument::metadata() noexcept
{
    return metadata_;
}

const WordSectionProperties& WordDocument::section() const noexcept
{
    return section_;
}

WordSectionProperties& WordDocument::section() noexcept
{
    return section_;
}

std::string WordDocument::plainText() const
{
    std::string result;
    const auto appendSeparator = [&result](char separator) {
        if (!result.empty() && result.back() != separator) {
            result.push_back(separator);
        }
    };

    for (const auto& block : blocks_) {
        if (const auto* paragraph = std::get_if<WordParagraph>(&block)) {
            appendSeparator('\n');
            result += paragraph->plainText();
            continue;
        }

        const auto& table = std::get<WordTable>(block);
        appendSeparator('\n');
        for (std::size_t rowIndex = 0; rowIndex < table.rows.size(); ++rowIndex) {
            const auto& row = table.rows[rowIndex];
            for (std::size_t cellIndex = 0; cellIndex < row.cells.size(); ++cellIndex) {
                const auto& cell = row.cells[cellIndex];
                for (std::size_t paragraphIndex = 0;
                     paragraphIndex < cell.paragraphs.size(); ++paragraphIndex) {
                    if (paragraphIndex > 0) {
                        result.push_back('\n');
                    }
                    result += cell.paragraphs[paragraphIndex].plainText();
                }
                if (cellIndex + 1 < row.cells.size()) {
                    result.push_back('\t');
                }
            }
            if (rowIndex + 1 < table.rows.size()) {
                result.push_back('\n');
            }
        }
    }
    return result;
}

} // namespace ii::document
