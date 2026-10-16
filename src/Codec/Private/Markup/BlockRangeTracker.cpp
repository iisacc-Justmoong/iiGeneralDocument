// Adapted from first-party iiHtmlBlock/Modifier/BlockRangeTracker; standard C++
// parser, UI bindings removed.
#include "BlockRangeTracker.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ii::document::detail::blocks {

namespace {

struct ParserInputParts {
    std::string prefix;
    std::string body;
};

bool IsAsciiSpace(char value) {
    return value == ' ' || value == '\n' || value == '\r' || value == '\t';
}

std::size_t SkipLeadingAscii(std::string_view source, std::size_t offset) {
    while (offset < source.size() && IsAsciiSpace(source[offset])) {
        ++offset;
    }

    return offset;
}

ParserInputParts SplitParserInput(std::string_view html) {
    std::size_t body_begin = SkipLeadingAscii(html, 0);

    bool consumed = true;
    while (consumed) {
        consumed = false;

        if (html.substr(body_begin).rfind("<?xml", 0) == 0) {
            const std::size_t end = html.find("?>", body_begin);
            if (end != std::string_view::npos) {
                body_begin = SkipLeadingAscii(html, end + 2);
                consumed = true;
            }
        }

        if (html.substr(body_begin).rfind("<!DOCTYPE", 0) == 0 ||
            html.substr(body_begin).rfind("<!doctype", 0) == 0) {
            const std::size_t end = html.find('>', body_begin);
            if (end != std::string_view::npos) {
                body_begin = SkipLeadingAscii(html, end + 1);
                consumed = true;
            }
        }
    }

    return ParserInputParts{std::string(html.substr(0, body_begin)),
                            std::string(html.substr(body_begin))};
}

bool RangesOverlap(std::size_t first_begin, std::size_t first_end, std::size_t second_begin,
                   std::size_t second_end) {
    return first_begin < second_end && second_begin < first_end;
}

} // namespace

BlockRangeTracker::BlockRangeTracker() : next_id_(1) {}

bool BlockRangeTracker::Parse(const char *html) {
    if (html == nullptr) {
        Clear();
        next_id_ = 1;
        error_ = "html input is null";

        return false;
    }

    return Parse(std::string_view(html));
}

bool BlockRangeTracker::Parse(std::string_view html) {

    Clear();
    next_id_ = 1;
    return BuildFromHtml(html, false);
}

bool BlockRangeTracker::Update(const char *html) {
    if (html == nullptr) {
        SetError("html input is null");

        return false;
    }

    return Update(std::string_view(html));
}

bool BlockRangeTracker::Update(std::string_view html) { return BuildFromHtml(html, true); }

void BlockRangeTracker::SetBlocks(const std::vector<DivideBlock::ElementInfo> &blocks) {
    html_.clear();
    tracked_blocks_.clear();
    error_.clear();
    next_id_ = 1;
    tracked_blocks_ = BuildTrackedBlocks(blocks, 0, false);
}

const std::string &BlockRangeTracker::GetHTMLText() const { return html_; }

const std::vector<BlockRangeTracker::TrackedBlock> &BlockRangeTracker::GetTrackedBlocks() const {
    return tracked_blocks_;
}

const BlockRangeTracker::TrackedBlock *BlockRangeTracker::GetBlockById(std::size_t id) const {
    const auto found = std::find_if(tracked_blocks_.begin(), tracked_blocks_.end(),
                                    [id](const TrackedBlock &block) { return block.id == id; });

    if (found == tracked_blocks_.end()) {
        return nullptr;
    }

    return &*found;
}

const BlockRangeTracker::TrackedBlock *
BlockRangeTracker::GetBlockByRange(std::size_t raw_begin, std::size_t raw_end) const {
    const auto found =
        std::find_if(tracked_blocks_.begin(), tracked_blocks_.end(),
                     [raw_begin, raw_end](const TrackedBlock &block) {
                         return block.raw_begin == raw_begin && block.raw_end == raw_end;
                     });

    if (found == tracked_blocks_.end()) {
        return nullptr;
    }

    return &*found;
}

const BlockRangeTracker::TrackedBlock *
BlockRangeTracker::GetInnermostBlockAt(std::size_t offset) const {
    const TrackedBlock *innermost = nullptr;
    std::size_t innermost_size = std::numeric_limits<std::size_t>::max();

    for (const TrackedBlock &block : tracked_blocks_) {
        if (offset < block.raw_begin || offset >= block.raw_end) {
            continue;
        }

        const std::size_t block_size = block.raw_end - block.raw_begin;
        if (block_size < innermost_size) {
            innermost = &block;
            innermost_size = block_size;
        }
    }

    return innermost;
}

std::vector<BlockRangeTracker::TrackedBlock>
BlockRangeTracker::GetBlocksOverlapping(std::size_t raw_begin, std::size_t raw_end) const {
    std::vector<TrackedBlock> overlapping;
    if (raw_begin >= raw_end) {
        return overlapping;
    }

    for (const TrackedBlock &block : tracked_blocks_) {
        if (RangesOverlap(raw_begin, raw_end, block.raw_begin, block.raw_end)) {
            overlapping.push_back(block);
        }
    }

    return overlapping;
}

const std::string &BlockRangeTracker::GetError() const { return error_; }

bool BlockRangeTracker::BuildFromHtml(std::string_view html, bool preserve_ids) {
    try {
        ParserInputParts parts = SplitParserInput(html);

        DivideBlock divider;
        if (!divider.Parse(parts.body)) {
            std::string parse_error = divider.GetError();
            if (parse_error.empty()) {
                parse_error = "block range tracking parse failed";
            }
            SetError(parse_error);

            return false;
        }

        std::vector<TrackedBlock> next_blocks =
            BuildTrackedBlocks(divider.GetBlockElements(), parts.prefix.size(), preserve_ids);

        html_ = std::string(html);
        tracked_blocks_ = std::move(next_blocks);
        error_.clear();

        return true;
    } catch (const std::exception &exception) {
        SetError(std::string("block range tracking exception: ") + exception.what());

        return false;
    } catch (...) {
        SetError("block range tracking exception: unknown");

        return false;
    }
}

std::vector<BlockRangeTracker::TrackedBlock>
BlockRangeTracker::BuildTrackedBlocks(const std::vector<DivideBlock::ElementInfo> &blocks,
                                      std::size_t document_offset, bool preserve_ids) {
    std::vector<TrackedBlock> tracked;
    tracked.reserve(blocks.size());

    std::vector<bool> used(tracked_blocks_.size(), false);
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const DivideBlock::ElementInfo &element = blocks[index];
        const std::size_t raw_begin = document_offset + element.raw_begin;
        const std::size_t raw_end = document_offset + element.raw_end;

        std::size_t id = next_id_++;
        if (preserve_ids) {
            const TrackedBlock *preserved = FindPreservedBlock(element, raw_begin, raw_end, used);
            if (preserved != nullptr) {
                id = preserved->id;
                const std::size_t previous_index =
                    static_cast<std::size_t>(preserved - tracked_blocks_.data());
                used[previous_index] = true;
            }
        }

        tracked.push_back(TrackedBlock{id, index, element, raw_begin,
                                       document_offset + element.value_begin,
                                       document_offset + element.value_end, raw_end});
    }

    return tracked;
}

const BlockRangeTracker::TrackedBlock *
BlockRangeTracker::FindPreservedBlock(const DivideBlock::ElementInfo &element,
                                      std::size_t raw_begin, std::size_t raw_end,
                                      const std::vector<bool> &used) const {
    for (std::size_t index = 0; index < tracked_blocks_.size(); ++index) {
        const TrackedBlock &block = tracked_blocks_[index];
        if (used[index]) {
            continue;
        }

        if (block.raw_begin == raw_begin && block.raw_end == raw_end &&
            block.element.tag_name == element.tag_name && block.element.raw == element.raw) {
            return &block;
        }
    }

    for (std::size_t index = 0; index < tracked_blocks_.size(); ++index) {
        const TrackedBlock &block = tracked_blocks_[index];
        if (used[index]) {
            continue;
        }

        if (block.element.tag_name == element.tag_name && block.element.raw == element.raw &&
            block.element.value == element.value) {
            return &block;
        }
    }

    return nullptr;
}

void BlockRangeTracker::Clear() {
    html_.clear();
    tracked_blocks_.clear();
    error_.clear();
}

void BlockRangeTracker::SetError(const std::string &error) { error_ = error; }

} // namespace ii::document::detail::blocks
