#include "Html/HtmlBlockDocument.h"

#include "Core/Diagnostic.h"

#include <iiHtmlBlock>

#include <algorithm>
#include <limits>
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

} // namespace ii::document
