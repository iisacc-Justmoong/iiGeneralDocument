#include "Html/HtmlBlockDocument.h"

#include "Core/Diagnostic.h"

#include <iiHtmlBlock>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace ii::document {
namespace {

bool isAsciiSpace(char value) noexcept
{
    return value == ' ' || value == '\n' || value == '\r' || value == '\t';
}

std::size_t skipAsciiSpace(std::string_view source, std::size_t offset) noexcept
{
    while (offset < source.size() && isAsciiSpace(source[offset])) {
        ++offset;
    }
    return offset;
}

std::string_view parserBody(std::string_view html) noexcept
{
    std::size_t bodyBegin = skipAsciiSpace(html, 0);
    bool consumed = true;
    while (consumed) {
        consumed = false;
        if (html.substr(bodyBegin).rfind("<?xml", 0) == 0) {
            const std::size_t end = html.find("?>", bodyBegin);
            if (end != std::string_view::npos) {
                bodyBegin = skipAsciiSpace(html, end + 2);
                consumed = true;
            }
        }
        if (html.substr(bodyBegin).rfind("<!DOCTYPE", 0) == 0
            || html.substr(bodyBegin).rfind("<!doctype", 0) == 0) {
            const std::size_t end = html.find('>', bodyBegin);
            if (end != std::string_view::npos) {
                bodyBegin = skipAsciiSpace(html, end + 1);
                consumed = true;
            }
        }
    }
    return html.substr(bodyBegin);
}

bool containsOnlyAsciiSpace(std::string_view value) noexcept
{
    return std::ranges::all_of(value, isAsciiSpace);
}

bool strictlyContains(const HtmlBlock& outer, const HtmlBlock& inner) noexcept
{
    return outer.rawBegin() <= inner.rawBegin()
        && inner.rawEnd() <= outer.rawEnd()
        && (outer.rawBegin() != inner.rawBegin() || outer.rawEnd() != inner.rawEnd());
}

} // namespace

HtmlBlock::HtmlBlock(
    HtmlBlockId id,
    std::size_t sourceIndex,
    std::string tagName,
    std::string value,
    std::string html,
    std::size_t rawBegin,
    std::size_t valueBegin,
    std::size_t valueEnd,
    std::size_t rawEnd,
    bool hasDisplayOverride,
    std::string displayValue)
    : id_(id),
      sourceIndex_(sourceIndex),
      tagName_(std::move(tagName)),
      value_(std::move(value)),
      html_(std::move(html)),
      rawBegin_(rawBegin),
      valueBegin_(valueBegin),
      valueEnd_(valueEnd),
      rawEnd_(rawEnd),
      hasDisplayOverride_(hasDisplayOverride),
      displayValue_(std::move(displayValue))
{
}

HtmlBlockId HtmlBlock::id() const noexcept
{
    return id_;
}

const std::optional<HtmlBlockId>& HtmlBlock::parentId() const noexcept
{
    return parentId_;
}

const std::vector<HtmlBlockId>& HtmlBlock::childIds() const noexcept
{
    return childIds_;
}

std::size_t HtmlBlock::depth() const noexcept
{
    return depth_;
}

const std::string& HtmlBlock::tagName() const noexcept
{
    return tagName_;
}

const std::string& HtmlBlock::value() const noexcept
{
    return value_;
}

const std::string& HtmlBlock::html() const noexcept
{
    return html_;
}

std::string_view HtmlBlock::openingTag() const noexcept
{
    if (valueBegin_ < rawBegin_ || valueBegin_ - rawBegin_ > html_.size()) {
        return {};
    }
    return std::string_view(html_).substr(0, valueBegin_ - rawBegin_);
}

std::string_view HtmlBlock::closingTag() const noexcept
{
    if (valueEnd_ < rawBegin_ || valueEnd_ - rawBegin_ > html_.size()) {
        return {};
    }
    return std::string_view(html_).substr(valueEnd_ - rawBegin_);
}

bool HtmlBlock::isSelfClosing() const noexcept
{
    return valueBegin_ == rawEnd_ && valueEnd_ == rawEnd_;
}

std::size_t HtmlBlock::rawBegin() const noexcept
{
    return rawBegin_;
}

std::size_t HtmlBlock::valueBegin() const noexcept
{
    return valueBegin_;
}

std::size_t HtmlBlock::valueEnd() const noexcept
{
    return valueEnd_;
}

std::size_t HtmlBlock::rawEnd() const noexcept
{
    return rawEnd_;
}

bool HtmlBlock::hasDisplayOverride() const noexcept
{
    return hasDisplayOverride_;
}

const std::string& HtmlBlock::displayValue() const noexcept
{
    return displayValue_;
}

HtmlBlockDocument HtmlBlockDocument::fromHtml(std::string html)
{
    HtmlBlockDocument document;
    document.blocks_ = parseBlocks(html);
    document.rootIds_ = rebuildHierarchy(document.blocks_);
    document.html_ = std::move(html);

    std::uint64_t maximumId = 0;
    for (const auto& block : document.blocks_) {
        maximumId = std::max(maximumId, block.id().value);
    }
    if (maximumId == std::numeric_limits<std::uint64_t>::max()) {
        throw DocumentError("HTML block id space is exhausted");
    }
    document.nextId_ = maximumId + 1;
    return document;
}

const std::string& HtmlBlockDocument::html() const noexcept
{
    return html_;
}

const std::vector<HtmlBlock>& HtmlBlockDocument::blocks() const noexcept
{
    return blocks_;
}

const std::vector<HtmlBlockId>& HtmlBlockDocument::rootIds() const noexcept
{
    return rootIds_;
}

const HtmlBlock* HtmlBlockDocument::find(HtmlBlockId id) const noexcept
{
    const auto found = std::ranges::find_if(blocks_, [id](const HtmlBlock& block) {
        return block.id() == id;
    });
    return found == blocks_.end() ? nullptr : &*found;
}

std::uint64_t HtmlBlockDocument::revision() const noexcept
{
    return revision_;
}

std::vector<HtmlBlock> HtmlBlockDocument::parseBlocks(std::string_view html)
{
    if (containsOnlyAsciiSpace(parserBody(html))) {
        return {};
    }

    iiHtmlBlock::BlockRangeTracker tracker;
    if (!tracker.Parse(html)) {
        std::string reason = tracker.GetError();
        if (reason.empty()) {
            reason = "unknown iiHtmlBlock parse failure";
        }
        throw DocumentError("HTML block parse failed: " + reason);
    }

    std::vector<HtmlBlock> blocks;
    blocks.reserve(tracker.GetTrackedBlocks().size());
    for (const auto& tracked : tracker.GetTrackedBlocks()) {
        if (tracked.id > std::numeric_limits<std::uint64_t>::max()) {
            throw DocumentError("HTML block id exceeds the public id range");
        }
        blocks.push_back(HtmlBlock(
            HtmlBlockId{static_cast<std::uint64_t>(tracked.id)},
            tracked.source_index,
            tracked.element.tag_name,
            tracked.element.value,
            tracked.element.raw,
            tracked.raw_begin,
            tracked.value_begin,
            tracked.value_end,
            tracked.raw_end,
            tracked.element.has_display_override,
            tracked.element.display_value));
    }
    return blocks;
}

std::vector<HtmlBlockId> HtmlBlockDocument::rebuildHierarchy(
    std::vector<HtmlBlock>& blocks)
{
    std::vector<std::optional<std::size_t>> parentIndexes(blocks.size());
    for (std::size_t childIndex = 0; childIndex < blocks.size(); ++childIndex) {
        for (std::size_t candidateIndex = 0; candidateIndex < blocks.size(); ++candidateIndex) {
            if (candidateIndex == childIndex
                || !strictlyContains(blocks[candidateIndex], blocks[childIndex])) {
                continue;
            }

            const auto currentParent = parentIndexes[childIndex];
            if (!currentParent.has_value()
                || blocks[candidateIndex].rawBegin() > blocks[*currentParent].rawBegin()
                || (blocks[candidateIndex].rawBegin() == blocks[*currentParent].rawBegin()
                    && blocks[candidateIndex].rawEnd() < blocks[*currentParent].rawEnd())) {
                parentIndexes[childIndex] = candidateIndex;
            }
        }
    }

    for (auto& block : blocks) {
        block.parentId_.reset();
        block.childIds_.clear();
        block.depth_ = 0;
    }

    constexpr std::size_t unresolved = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> depths(blocks.size(), unresolved);
    const auto resolveDepth = [&](const auto& self, std::size_t index) -> std::size_t {
        if (depths[index] != unresolved) {
            return depths[index];
        }
        if (!parentIndexes[index].has_value()) {
            depths[index] = 0;
            return 0;
        }
        const std::size_t parentDepth = self(self, *parentIndexes[index]);
        if (parentDepth == std::numeric_limits<std::size_t>::max() - 1) {
            throw DocumentError("HTML block hierarchy depth is exhausted");
        }
        depths[index] = parentDepth + 1;
        return depths[index];
    };

    std::vector<HtmlBlockId> roots;
    roots.reserve(blocks.size());
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        blocks[index].depth_ = resolveDepth(resolveDepth, index);
        if (!parentIndexes[index].has_value()) {
            roots.push_back(blocks[index].id());
            continue;
        }

        HtmlBlock& parent = blocks[*parentIndexes[index]];
        blocks[index].parentId_ = parent.id();
        parent.childIds_.push_back(blocks[index].id());
    }
    return roots;
}

} // namespace ii::document
