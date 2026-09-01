#pragma once

#include "Html/HtmlBlockDocument.h"
#include "iiGeneralDocument/Export.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document {

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocumentHeader {
    std::map<std::string, std::string> metadata;
};

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocumentBody {
    HtmlBlockDocument htmlBlocks;
};

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceTextDiff {
    std::size_t commonPrefixBytes{0};
    std::size_t commonSuffixBytes{0};
    std::string removedText;
    std::string insertedText;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::optional<std::string> apply(std::string_view base) const;
};

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocumentSnapshot {
    std::string objectId;
    std::string headerObjectId;
    std::string bodyObjectId;
    std::map<std::string, std::string> headerMetadata;
    std::string bodyHtml;
};

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocumentDiff {
    std::string objectId;
    std::string baseSnapshotObjectId;
    std::string targetSnapshotObjectId;
    ThinkingSpaceTextDiff header;
    ThinkingSpaceTextDiff body;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::optional<ThinkingSpaceDocumentSnapshot> apply(
        const ThinkingSpaceDocumentSnapshot& base) const;
};

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocumentVersion {
    std::string objectId;
    std::string parentObjectId;
    std::string snapshotObjectId;
    std::string diffObjectId;
    std::string label;
    std::string createdAtUtc;
};

class IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocumentVersionHistory {
public:
    static constexpr std::size_t maximumVersionCount{100};

    [[nodiscard]] ThinkingSpaceDocumentVersion record(
        const ThinkingSpaceDocumentHeader& header,
        const ThinkingSpaceDocumentBody& body,
        std::string label = {},
        std::string createdAtUtc = {});

    [[nodiscard]] const std::vector<ThinkingSpaceDocumentVersion>& versions() const noexcept;
    [[nodiscard]] const std::map<std::string, ThinkingSpaceDocumentSnapshot>&
    snapshots() const noexcept;
    [[nodiscard]] const std::map<std::string, ThinkingSpaceDocumentDiff>&
    diffs() const noexcept;
    [[nodiscard]] const ThinkingSpaceDocumentVersion* head() const noexcept;
    [[nodiscard]] const ThinkingSpaceDocumentVersion* findVersion(
        std::string_view objectId) const noexcept;
    [[nodiscard]] const ThinkingSpaceDocumentSnapshot* findSnapshot(
        std::string_view objectId) const noexcept;
    [[nodiscard]] const ThinkingSpaceDocumentDiff* findDiff(
        std::string_view objectId) const noexcept;
    [[nodiscard]] std::uint64_t prunedVersionCount() const noexcept;
    [[nodiscard]] bool hasPrunedHistory() const noexcept;
    [[nodiscard]] const std::string& shallowBoundaryParentObjectId() const noexcept;
    [[nodiscard]] bool verifyIntegrity() const;

private:
    void pruneOldestVersions();
    void collectUnreferencedObjects();

    std::vector<ThinkingSpaceDocumentVersion> versions_;
    std::map<std::string, ThinkingSpaceDocumentSnapshot> snapshots_;
    std::map<std::string, ThinkingSpaceDocumentDiff> diffs_;
    std::uint64_t prunedVersionCount_{0};
    std::string shallowBoundaryParentObjectId_;
};

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocument {
    static constexpr std::string_view fileExtension{".tsdoc"};

    ThinkingSpaceDocumentHeader header;
    ThinkingSpaceDocumentBody body;
    ThinkingSpaceDocumentVersionHistory versionHistory;

    [[nodiscard]] ThinkingSpaceDocumentVersion recordVersion(
        std::string label = {},
        std::string createdAtUtc = {});
};

} // namespace ii::document
