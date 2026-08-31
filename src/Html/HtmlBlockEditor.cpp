#include "Html/HtmlBlockEditor.h"

#include "Core/Diagnostic.h"

#include <iiHtmlBlock>

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace ii::document {
namespace {

bool isAsciiSpace(char value) noexcept
{
    return value == ' ' || value == '\n' || value == '\r' || value == '\t';
}

std::string_view trimAscii(std::string_view value) noexcept
{
    while (!value.empty() && isAsciiSpace(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && isAsciiSpace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

bool strictlyContains(
    const iiHtmlBlock::BlockRangeTracker::TrackedBlock& outer,
    const iiHtmlBlock::BlockRangeTracker::TrackedBlock& inner) noexcept
{
    return outer.raw_begin <= inner.raw_begin
        && inner.raw_end <= outer.raw_end
        && (outer.raw_begin != inner.raw_begin || outer.raw_end != inner.raw_end);
}

std::size_t checkedAdd(std::size_t left, std::size_t right)
{
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw DocumentError("HTML block source range overflow");
    }
    return left + right;
}

std::size_t shiftedAfterEdit(
    std::size_t position,
    std::size_t editBegin,
    std::size_t editEnd,
    std::size_t replacementSize)
{
    return checkedAdd(checkedAdd(editBegin, replacementSize), position - editEnd);
}

std::size_t expandedAncestorEnd(
    std::size_t oldEnd,
    std::size_t removedSize,
    std::size_t replacementSize)
{
    return checkedAdd(oldEnd - removedSize, replacementSize);
}

std::uint64_t takeNextId(std::uint64_t& nextId)
{
    if (nextId == 0 || nextId == std::numeric_limits<std::uint64_t>::max()) {
        throw DocumentError("HTML block id space is exhausted");
    }
    return nextId++;
}

std::size_t findBlockByRange(
    const std::vector<HtmlBlock>& blocks,
    const std::vector<bool>& assigned,
    std::size_t begin,
    std::size_t end,
    const std::string* tagName)
{
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (assigned[index]) {
            continue;
        }
        const auto& block = blocks[index];
        if (block.rawBegin() == begin
            && block.rawEnd() == end
            && (tagName == nullptr || block.tagName() == *tagName)) {
            return index;
        }
    }
    return blocks.size();
}

bool rangesOverlap(
    std::size_t firstBegin,
    std::size_t firstEnd,
    std::size_t secondBegin,
    std::size_t secondEnd) noexcept
{
    return firstBegin < secondEnd && secondBegin < firstEnd;
}

} // namespace

HtmlBlockEditor::HtmlBlockEditor(HtmlBlockDocument& document) noexcept
    : document_(document)
{
}

HtmlBlockId HtmlBlockEditor::create(
    std::string blockHtml,
    std::optional<HtmlBlockId> parent)
{
    std::string serialized = serializeSingleBlock(blockHtml);
    std::size_t insertion = document_.html_.size();
    if (parent.has_value()) {
        const HtmlBlock* parentBlock = read(*parent);
        if (parentBlock == nullptr) {
            throw DocumentError("HTML block create parent was not found");
        }
        insertion = parentBlock->valueEnd();
    }

    if (document_.nextId_ == 0
        || document_.nextId_ == std::numeric_limits<std::uint64_t>::max()) {
        throw DocumentError("HTML block id space is exhausted");
    }
    const HtmlBlockId createdId{document_.nextId_};
    replaceRange(insertion, insertion, std::move(serialized), createdId, true);
    return createdId;
}

const HtmlBlock* HtmlBlockEditor::read(HtmlBlockId id) const noexcept
{
    return document_.find(id);
}

void HtmlBlockEditor::update(HtmlBlockId id, std::string blockHtml)
{
    const HtmlBlock* target = read(id);
    if (target == nullptr) {
        throw DocumentError("HTML block update target was not found");
    }
    std::string serialized = serializeSingleBlock(blockHtml);
    if (serialized == target->html()) {
        return;
    }
    const std::size_t begin = target->rawBegin();
    const std::size_t end = target->rawEnd();
    replaceRange(begin, end, std::move(serialized), id, false);
}

bool HtmlBlockEditor::remove(HtmlBlockId id)
{
    const HtmlBlock* target = read(id);
    if (target == nullptr) {
        return false;
    }

    const std::size_t begin = target->rawBegin();
    const std::size_t end = target->rawEnd();
    const std::size_t sourceIndex = target->sourceIndex_;

    std::string expected = document_.html_;
    expected.erase(begin, end - begin);

    iiHtmlBlock::DeleteBlock deleter;
    if (!deleter.Parse(document_.html_)) {
        throw DocumentError("HTML block delete parse failed: " + deleter.GetError());
    }
    if (sourceIndex >= deleter.GetBlocks().size()) {
        throw DocumentError("HTML block delete source index is out of range");
    }
    if (deleter.Delete(sourceIndex)) {
        if (deleter.GetHTMLText() != expected) {
            throw DocumentError("HTML block delete backend produced an unexpected source");
        }
    } else {
        static_cast<void>(HtmlBlockDocument::parseBlocks(expected));
    }

    replaceRange(begin, end, {}, std::nullopt, false);
    return true;
}

std::string HtmlBlockEditor::serializeSingleBlock(std::string_view blockHtml)
{
    const std::string_view trimmed = trimAscii(blockHtml);
    if (trimmed.empty()) {
        throw DocumentError("HTML block fragment is empty");
    }

    iiHtmlBlock::BlockRangeTracker tracker;
    if (!tracker.Parse(trimmed)) {
        std::string reason = tracker.GetError();
        if (reason.empty()) {
            reason = "unknown iiHtmlBlock parse failure";
        }
        throw DocumentError("HTML block fragment parse failed: " + reason);
    }

    const auto& blocks = tracker.GetTrackedBlocks();
    std::vector<std::size_t> topLevelIndexes;
    for (std::size_t candidateIndex = 0; candidateIndex < blocks.size(); ++candidateIndex) {
        bool contained = false;
        for (std::size_t otherIndex = 0; otherIndex < blocks.size(); ++otherIndex) {
            if (candidateIndex != otherIndex
                && strictlyContains(blocks[otherIndex], blocks[candidateIndex])) {
                contained = true;
                break;
            }
        }
        if (!contained) {
            topLevelIndexes.push_back(candidateIndex);
        }
    }

    if (topLevelIndexes.size() != 1) {
        throw DocumentError("HTML block fragment must contain exactly one top-level block");
    }

    const auto& root = blocks[topLevelIndexes.front()];
    if (root.raw_begin != 0 || root.raw_end != trimmed.size()) {
        throw DocumentError("HTML block fragment contains content outside its block root");
    }

    iiHtmlBlock::BlockHTMLSerializer serializer;
    if (!serializer.SerializeBlocks({root.element})) {
        std::string reason = serializer.GetError();
        if (reason.empty()) {
            reason = "unknown iiHtmlBlock serialization failure";
        }
        throw DocumentError("HTML block fragment serialization failed: " + reason);
    }
    if (serializer.GetHTMLText() != trimmed) {
        throw DocumentError("HTML block fragment serialization changed its root source");
    }
    return serializer.GetHTMLText();
}

void HtmlBlockEditor::replaceRange(
    std::size_t begin,
    std::size_t end,
    std::string replacement,
    std::optional<HtmlBlockId> replacementRootId,
    bool replacementRootIsNew)
{
    if (begin > end || end > document_.html_.size()) {
        throw DocumentError("HTML block edit range is out of source bounds");
    }
    if (document_.revision_ == std::numeric_limits<std::uint64_t>::max()) {
        throw DocumentError("HTML block revision space is exhausted");
    }

    std::string nextHtml = document_.html_;
    nextHtml.replace(begin, end - begin, replacement);
    std::vector<HtmlBlock> nextBlocks = HtmlBlockDocument::parseBlocks(nextHtml);
    std::vector<bool> assigned(nextBlocks.size(), false);

    std::uint64_t nextId = document_.nextId_;
    if (replacementRootId.has_value()) {
        const std::size_t replacementEnd = checkedAdd(begin, replacement.size());
        const std::size_t index = findBlockByRange(
            nextBlocks, assigned, begin, replacementEnd, nullptr);
        if (index == nextBlocks.size()) {
            throw DocumentError("HTML block replacement root was not found after parsing");
        }
        if (replacementRootIsNew) {
            const std::uint64_t allocated = takeNextId(nextId);
            if (allocated != replacementRootId->value) {
                throw DocumentError("HTML block id allocation state is inconsistent");
            }
        }
        nextBlocks[index].id_ = *replacementRootId;
        assigned[index] = true;
    }

    const std::size_t removedSize = end - begin;
    for (const auto& oldBlock : document_.blocks_) {
        if (replacementRootId.has_value()
            && !replacementRootIsNew
            && oldBlock.id() == *replacementRootId) {
            continue;
        }

        std::size_t expectedBegin = 0;
        std::size_t expectedEnd = 0;
        bool preserve = false;

        if (oldBlock.rawBegin() == begin && oldBlock.rawEnd() == end) {
            preserve = false;
        } else if (oldBlock.rawEnd() <= begin) {
            expectedBegin = oldBlock.rawBegin();
            expectedEnd = oldBlock.rawEnd();
            preserve = true;
        } else if (oldBlock.rawBegin() >= end) {
            expectedBegin = shiftedAfterEdit(
                oldBlock.rawBegin(), begin, end, replacement.size());
            expectedEnd = shiftedAfterEdit(
                oldBlock.rawEnd(), begin, end, replacement.size());
            preserve = true;
        } else if (oldBlock.rawBegin() < begin && oldBlock.rawEnd() > end) {
            expectedBegin = oldBlock.rawBegin();
            expectedEnd = expandedAncestorEnd(
                oldBlock.rawEnd(), removedSize, replacement.size());
            preserve = true;
        } else if (rangesOverlap(oldBlock.rawBegin(), oldBlock.rawEnd(), begin, end)) {
            if (!(begin <= oldBlock.rawBegin() && oldBlock.rawEnd() <= end)) {
                throw DocumentError("HTML block edit crosses an overlapping block range");
            }
        }

        if (!preserve) {
            continue;
        }

        const std::size_t index = findBlockByRange(
            nextBlocks, assigned, expectedBegin, expectedEnd, &oldBlock.tagName_);
        if (index == nextBlocks.size()) {
            throw DocumentError("HTML block identity could not be preserved after editing");
        }
        nextBlocks[index].id_ = oldBlock.id();
        assigned[index] = true;
    }

    for (std::size_t index = 0; index < nextBlocks.size(); ++index) {
        if (!assigned[index]) {
            nextBlocks[index].id_ = HtmlBlockId{takeNextId(nextId)};
        }
        nextBlocks[index].sourceIndex_ = index;
    }

    document_.html_ = std::move(nextHtml);
    document_.blocks_ = std::move(nextBlocks);
    document_.nextId_ = nextId;
    ++document_.revision_;
}

} // namespace ii::document
