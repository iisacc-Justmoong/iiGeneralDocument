// Adapted from first-party iiHtmlBlock/Modifier/BlockRangeTracker; standard C++
// parser, UI bindings removed.
#pragma once

#include "Codec/Private/Markup/DivideBlock.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document::detail::blocks {

class BlockRangeTracker {

  public:
    struct TrackedBlock {
        std::size_t id;
        std::size_t source_index;
        DivideBlock::ElementInfo element;
        std::size_t raw_begin;
        std::size_t value_begin;
        std::size_t value_end;
        std::size_t raw_end;
    };

    explicit BlockRangeTracker();

    [[nodiscard]] bool Parse(const char *html);
    [[nodiscard]] bool Parse(std::string_view html);

    [[nodiscard]] bool Update(const char *html);
    [[nodiscard]] bool Update(std::string_view html);

    void SetBlocks(const std::vector<DivideBlock::ElementInfo> &blocks);

    [[nodiscard]] const std::string &GetHTMLText() const;
    [[nodiscard]] const std::vector<TrackedBlock> &GetTrackedBlocks() const;
    [[nodiscard]] const TrackedBlock *GetBlockById(std::size_t id) const;
    [[nodiscard]] const TrackedBlock *GetBlockByRange(std::size_t raw_begin,
                                                      std::size_t raw_end) const;
    [[nodiscard]] const TrackedBlock *GetInnermostBlockAt(std::size_t offset) const;
    [[nodiscard]] std::vector<TrackedBlock> GetBlocksOverlapping(std::size_t raw_begin,
                                                                 std::size_t raw_end) const;
    [[nodiscard]] const std::string &GetError() const;

  private:
    [[nodiscard]] bool BuildFromHtml(std::string_view html, bool preserve_ids);
    [[nodiscard]] std::vector<TrackedBlock>
    BuildTrackedBlocks(const std::vector<DivideBlock::ElementInfo> &blocks,
                       std::size_t document_offset, bool preserve_ids);
    [[nodiscard]] const TrackedBlock *FindPreservedBlock(const DivideBlock::ElementInfo &element,
                                                         std::size_t raw_begin, std::size_t raw_end,
                                                         const std::vector<bool> &used) const;
    void Clear();
    void SetError(const std::string &error);

    std::string html_;
    std::vector<TrackedBlock> tracked_blocks_;
    std::string error_;
    std::size_t next_id_;
};

} // namespace ii::document::detail::blocks
