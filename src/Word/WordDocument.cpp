#include "Word/WordDocument.h"
#include "Metadata/Authorship_p.hpp"

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
    recordChange();
}

void WordDocument::appendTable(WordTable table)
{
    blocks_.emplace_back(std::move(table));
    recordChange();
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


const iiFileProvider::Authorship& WordDocument::authorship() const noexcept { return authorship_; }
bool WordDocument::setFileAuthor(const iiFileProvider::FileAuthor& author) {
    auto next = authorship_; const bool changed = next.setAuthor(author);
    detail::storeAuthorship(next, metadata_); authorship_ = std::move(next); return changed;
}
void WordDocument::recordChange() {
    auto next = authorship_; next.recordChange();
    detail::storeAuthorship(next, metadata_); authorship_ = std::move(next);
}
void WordDocument::restoreAuthorship() { authorship_ = detail::readAuthorship(metadata_); }
bool WordDocument::setMetadata(std::string key, std::string value) {
    if (key == iiFileProvider::Authorship::MetadataKey) throw DocumentError("Authorship is managed by iiFileProvider");
    const auto found = metadata_.find(key);
    if (found != metadata_.end() && found->second == value) return false;
    auto next = metadata_; next[std::move(key)] = std::move(value);
    auto author = authorship_; author.recordChange(); detail::storeAuthorship(author, next);
    metadata_ = std::move(next); authorship_ = std::move(author); return true;
}

bool WordDocument::edit(const std::function<bool(WordDocument&)>& callback) {
    if (!callback) throw DocumentError("Empty Word edit callback");
    auto draft = *this;
    if (!callback(draft)) return false;
    const bool changed = draft.blocks_ != blocks_ || draft.section_ != section_ || draft.metadata_ != metadata_;
    if (changed && draft.authorship_.dump() == authorship_.dump()) draft.recordChange();
    *this = std::move(draft); return changed;
}

} // namespace ii::document
