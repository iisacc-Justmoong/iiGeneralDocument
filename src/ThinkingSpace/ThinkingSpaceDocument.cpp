#include "ThinkingSpace/ThinkingSpaceDocument.h"

#include "Core/Diagnostic.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>

#include <algorithm>
#include <charconv>
#include <limits>
#include <utility>

namespace ii::document {
namespace {

void appendLengthPrefixed(std::string& target, std::string_view value)
{
    target += std::to_string(value.size());
    target.push_back(':');
    target.append(value);
}

std::string hashObject(std::string_view type, std::string_view payload)
{
    std::string framed;
    framed.append(type);
    framed.push_back(' ');
    framed += std::to_string(payload.size());
    framed.push_back('\0');
    framed.append(payload);

    return QCryptographicHash::hash(
               QByteArray::fromStdString(framed),
               QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

std::string canonicalHeader(const std::map<std::string, std::string>& metadata)
{
    std::string canonical;
    canonical += std::to_string(metadata.size());
    canonical.push_back('\n');
    for (const auto& [name, value] : metadata) {
        appendLengthPrefixed(canonical, name);
        appendLengthPrefixed(canonical, value);
        canonical.push_back('\n');
    }
    return canonical;
}

bool readSize(
    std::string_view source,
    std::size_t& offset,
    char delimiter,
    std::size_t& value)
{
    const std::size_t end = source.find(delimiter, offset);
    if (end == std::string_view::npos || end == offset) {
        return false;
    }
    const char* beginPointer = source.data() + offset;
    const char* endPointer = source.data() + end;
    const auto result = std::from_chars(beginPointer, endPointer, value);
    if (result.ec != std::errc{} || result.ptr != endPointer) {
        return false;
    }
    offset = end + 1;
    return true;
}

std::optional<std::map<std::string, std::string>> parseCanonicalHeader(
    std::string_view canonical)
{
    std::size_t offset = 0;
    std::size_t entryCount = 0;
    if (!readSize(canonical, offset, '\n', entryCount)) {
        return std::nullopt;
    }

    std::map<std::string, std::string> metadata;
    for (std::size_t index = 0; index < entryCount; ++index) {
        std::size_t nameSize = 0;
        if (!readSize(canonical, offset, ':', nameSize)
            || nameSize > canonical.size() - offset) {
            return std::nullopt;
        }
        std::string name(canonical.substr(offset, nameSize));
        offset += nameSize;

        std::size_t valueSize = 0;
        if (!readSize(canonical, offset, ':', valueSize)
            || valueSize > canonical.size() - offset) {
            return std::nullopt;
        }
        std::string value(canonical.substr(offset, valueSize));
        offset += valueSize;
        if (offset >= canonical.size() || canonical[offset] != '\n') {
            return std::nullopt;
        }
        ++offset;
        if (!metadata.emplace(std::move(name), std::move(value)).second) {
            return std::nullopt;
        }
    }
    if (offset != canonical.size()) {
        return std::nullopt;
    }
    return metadata;
}

ThinkingSpaceTextDiff makeTextDiff(std::string_view before, std::string_view after)
{
    ThinkingSpaceTextDiff diff;
    const std::size_t sharedLimit = std::min(before.size(), after.size());
    while (diff.commonPrefixBytes < sharedLimit
           && before[diff.commonPrefixBytes] == after[diff.commonPrefixBytes]) {
        ++diff.commonPrefixBytes;
    }

    const std::size_t suffixLimit = sharedLimit - diff.commonPrefixBytes;
    while (diff.commonSuffixBytes < suffixLimit
           && before[before.size() - 1 - diff.commonSuffixBytes]
               == after[after.size() - 1 - diff.commonSuffixBytes]) {
        ++diff.commonSuffixBytes;
    }

    const std::size_t removedLength =
        before.size() - diff.commonPrefixBytes - diff.commonSuffixBytes;
    const std::size_t insertedLength =
        after.size() - diff.commonPrefixBytes - diff.commonSuffixBytes;
    diff.removedText = std::string(before.substr(diff.commonPrefixBytes, removedLength));
    diff.insertedText = std::string(after.substr(diff.commonPrefixBytes, insertedLength));
    return diff;
}

std::string canonicalTextDiff(const ThinkingSpaceTextDiff& diff)
{
    std::string canonical;
    canonical += std::to_string(diff.commonPrefixBytes);
    canonical.push_back('\n');
    canonical += std::to_string(diff.commonSuffixBytes);
    canonical.push_back('\n');
    appendLengthPrefixed(canonical, diff.removedText);
    appendLengthPrefixed(canonical, diff.insertedText);
    return canonical;
}

ThinkingSpaceDocumentSnapshot makeSnapshotFromContent(
    const std::map<std::string, std::string>& metadata,
    std::string_view bodyHtml)
{
    ThinkingSpaceDocumentSnapshot snapshot;
    snapshot.headerMetadata = metadata;
    snapshot.bodyHtml = bodyHtml;

    const std::string headerPayload = canonicalHeader(snapshot.headerMetadata);
    snapshot.headerObjectId = hashObject("blob", headerPayload);
    snapshot.bodyObjectId = hashObject("blob", snapshot.bodyHtml);

    std::string treePayload;
    treePayload.reserve(snapshot.headerObjectId.size() + snapshot.bodyObjectId.size() + 16);
    treePayload += "header ";
    treePayload += snapshot.headerObjectId;
    treePayload += "\nbody ";
    treePayload += snapshot.bodyObjectId;
    treePayload.push_back('\n');
    snapshot.objectId = hashObject("tree", treePayload);
    return snapshot;
}

ThinkingSpaceDocumentSnapshot makeSnapshot(
    const ThinkingSpaceDocumentHeader& header,
    const ThinkingSpaceDocumentBody& body)
{
    return makeSnapshotFromContent(header.metadata, body.htmlBlocks.html());
}

ThinkingSpaceDocumentSnapshot makeEmptySnapshot()
{
    return makeSnapshot(ThinkingSpaceDocumentHeader{}, ThinkingSpaceDocumentBody{});
}

std::string canonicalDiff(const ThinkingSpaceDocumentDiff& diff)
{
    std::string payload;
    payload += "base ";
    payload += diff.baseSnapshotObjectId;
    payload += "\ntarget ";
    payload += diff.targetSnapshotObjectId;
    payload += "\nheader ";
    appendLengthPrefixed(payload, canonicalTextDiff(diff.header));
    payload += "\nbody ";
    appendLengthPrefixed(payload, canonicalTextDiff(diff.body));
    payload.push_back('\n');
    return payload;
}

ThinkingSpaceDocumentDiff makeDiff(
    const ThinkingSpaceDocumentSnapshot& base,
    const ThinkingSpaceDocumentSnapshot& target)
{
    ThinkingSpaceDocumentDiff diff;
    diff.baseSnapshotObjectId = base.objectId;
    diff.targetSnapshotObjectId = target.objectId;
    diff.header = makeTextDiff(
        canonicalHeader(base.headerMetadata), canonicalHeader(target.headerMetadata));
    diff.body = makeTextDiff(base.bodyHtml, target.bodyHtml);
    diff.objectId = hashObject("diff", canonicalDiff(diff));
    return diff;
}

std::string canonicalVersion(const ThinkingSpaceDocumentVersion& version)
{
    std::string payload;
    payload += "tree ";
    payload += version.snapshotObjectId;
    payload.push_back('\n');
    if (!version.parentObjectId.empty()) {
        payload += "parent ";
        payload += version.parentObjectId;
        payload.push_back('\n');
    }
    payload += "diff ";
    payload += version.diffObjectId;
    payload += "\ncreated ";
    appendLengthPrefixed(payload, version.createdAtUtc);
    payload += "\nlabel ";
    appendLengthPrefixed(payload, version.label);
    payload.push_back('\n');
    return payload;
}

std::string currentUtcTimestamp()
{
    return QDateTime::currentDateTimeUtc()
        .toString(Qt::ISODateWithMs)
        .toStdString();
}

bool snapshotMatchesId(const ThinkingSpaceDocumentSnapshot& snapshot)
{
    const ThinkingSpaceDocumentSnapshot rebuilt =
        makeSnapshotFromContent(snapshot.headerMetadata, snapshot.bodyHtml);
    return rebuilt.objectId == snapshot.objectId
        && rebuilt.headerObjectId == snapshot.headerObjectId
        && rebuilt.bodyObjectId == snapshot.bodyObjectId;
}

} // namespace

bool ThinkingSpaceTextDiff::empty() const noexcept
{
    return removedText.empty() && insertedText.empty();
}

std::optional<std::string> ThinkingSpaceTextDiff::apply(std::string_view base) const
{
    if (commonPrefixBytes > base.size()
        || commonSuffixBytes > base.size() - commonPrefixBytes) {
        return std::nullopt;
    }

    const std::size_t removedLength = base.size() - commonPrefixBytes - commonSuffixBytes;
    if (removedLength != removedText.size()
        || base.substr(commonPrefixBytes, removedLength) != removedText) {
        return std::nullopt;
    }
    if (insertedText.size() > std::numeric_limits<std::size_t>::max()
            - commonPrefixBytes - commonSuffixBytes) {
        return std::nullopt;
    }

    std::string result;
    result.reserve(commonPrefixBytes + insertedText.size() + commonSuffixBytes);
    result.append(base.substr(0, commonPrefixBytes));
    result += insertedText;
    result.append(base.substr(base.size() - commonSuffixBytes));
    return result;
}

bool ThinkingSpaceDocumentDiff::empty() const noexcept
{
    return header.empty() && body.empty();
}

std::optional<ThinkingSpaceDocumentSnapshot> ThinkingSpaceDocumentDiff::apply(
    const ThinkingSpaceDocumentSnapshot& base) const
{
    const ThinkingSpaceDocumentSnapshot normalizedBase =
        makeSnapshotFromContent(base.headerMetadata, base.bodyHtml);
    if ((!base.objectId.empty() && !snapshotMatchesId(base))
        || normalizedBase.objectId != baseSnapshotObjectId) {
        return std::nullopt;
    }

    const auto headerResult = header.apply(canonicalHeader(base.headerMetadata));
    const auto bodyResult = body.apply(base.bodyHtml);
    if (!headerResult.has_value() || !bodyResult.has_value()) {
        return std::nullopt;
    }
    auto metadata = parseCanonicalHeader(*headerResult);
    if (!metadata.has_value()) {
        return std::nullopt;
    }

    ThinkingSpaceDocumentSnapshot target =
        makeSnapshotFromContent(*metadata, *bodyResult);
    if (target.objectId != targetSnapshotObjectId) {
        return std::nullopt;
    }
    return target;
}

ThinkingSpaceDocumentVersion ThinkingSpaceDocumentVersionHistory::record(
    const ThinkingSpaceDocumentHeader& header,
    const ThinkingSpaceDocumentBody& body,
    std::string label,
    std::string createdAtUtc)
{
    if (createdAtUtc.empty()) {
        createdAtUtc = currentUtcTimestamp();
    }
    if (versions_.size() >= maximumVersionCount
        && prunedVersionCount_ == std::numeric_limits<std::uint64_t>::max()) {
        throw DocumentError("Thinking Space pruned version counter is exhausted");
    }

    const ThinkingSpaceDocumentVersion* parent = head();
    ThinkingSpaceDocumentSnapshot baseSnapshot = makeEmptySnapshot();
    if (parent != nullptr) {
        const ThinkingSpaceDocumentSnapshot* resolved = findSnapshot(parent->snapshotObjectId);
        if (resolved == nullptr) {
            throw DocumentError("Thinking Space history head snapshot is missing");
        }
        baseSnapshot = *resolved;
    }

    ThinkingSpaceDocumentSnapshot targetSnapshot = makeSnapshot(header, body);
    ThinkingSpaceDocumentDiff diff = makeDiff(baseSnapshot, targetSnapshot);
    ThinkingSpaceDocumentVersion version;
    version.parentObjectId = parent == nullptr ? std::string{} : parent->objectId;
    version.snapshotObjectId = targetSnapshot.objectId;
    version.diffObjectId = diff.objectId;
    version.label = std::move(label);
    version.createdAtUtc = std::move(createdAtUtc);
    version.objectId = hashObject("commit", canonicalVersion(version));

    const std::string snapshotObjectId = targetSnapshot.objectId;
    const std::string diffObjectId = diff.objectId;
    const auto [snapshotIterator, snapshotInserted] =
        snapshots_.try_emplace(snapshotObjectId, std::move(targetSnapshot));
    static_cast<void>(snapshotIterator);

    bool diffInserted = false;
    try {
        const auto result = diffs_.try_emplace(diffObjectId, std::move(diff));
        diffInserted = result.second;
        versions_.push_back(version);
    } catch (...) {
        if (diffInserted) {
            diffs_.erase(diffObjectId);
        }
        if (snapshotInserted) {
            snapshots_.erase(snapshotObjectId);
        }
        throw;
    }
    pruneOldestVersions();
    return version;
}

const std::vector<ThinkingSpaceDocumentVersion>&
ThinkingSpaceDocumentVersionHistory::versions() const noexcept
{
    return versions_;
}

const std::map<std::string, ThinkingSpaceDocumentSnapshot>&
ThinkingSpaceDocumentVersionHistory::snapshots() const noexcept
{
    return snapshots_;
}

const std::map<std::string, ThinkingSpaceDocumentDiff>&
ThinkingSpaceDocumentVersionHistory::diffs() const noexcept
{
    return diffs_;
}

const ThinkingSpaceDocumentVersion* ThinkingSpaceDocumentVersionHistory::head() const noexcept
{
    return versions_.empty() ? nullptr : &versions_.back();
}

const ThinkingSpaceDocumentVersion* ThinkingSpaceDocumentVersionHistory::findVersion(
    std::string_view objectId) const noexcept
{
    const auto found = std::ranges::find_if(versions_, [objectId](const auto& version) {
        return version.objectId == objectId;
    });
    return found == versions_.end() ? nullptr : &*found;
}

const ThinkingSpaceDocumentSnapshot* ThinkingSpaceDocumentVersionHistory::findSnapshot(
    std::string_view objectId) const noexcept
{
    const auto found = std::ranges::find_if(snapshots_, [objectId](const auto& entry) {
        return entry.first == objectId;
    });
    return found == snapshots_.end() ? nullptr : &found->second;
}

const ThinkingSpaceDocumentDiff* ThinkingSpaceDocumentVersionHistory::findDiff(
    std::string_view objectId) const noexcept
{
    const auto found = std::ranges::find_if(diffs_, [objectId](const auto& entry) {
        return entry.first == objectId;
    });
    return found == diffs_.end() ? nullptr : &found->second;
}

std::uint64_t ThinkingSpaceDocumentVersionHistory::prunedVersionCount() const noexcept
{
    return prunedVersionCount_;
}

bool ThinkingSpaceDocumentVersionHistory::hasPrunedHistory() const noexcept
{
    return prunedVersionCount_ != 0;
}

const std::string&
ThinkingSpaceDocumentVersionHistory::shallowBoundaryParentObjectId() const noexcept
{
    return shallowBoundaryParentObjectId_;
}

bool ThinkingSpaceDocumentVersionHistory::verifyIntegrity() const
{
    if (versions_.size() > maximumVersionCount) {
        return false;
    }
    if ((prunedVersionCount_ == 0) != shallowBoundaryParentObjectId_.empty()) {
        return false;
    }

    const ThinkingSpaceDocumentSnapshot emptySnapshot = makeEmptySnapshot();
    for (std::size_t index = 0; index < versions_.size(); ++index) {
        const ThinkingSpaceDocumentVersion& version = versions_[index];
        const ThinkingSpaceDocumentSnapshot* snapshot = findSnapshot(version.snapshotObjectId);
        const ThinkingSpaceDocumentDiff* diff = findDiff(version.diffObjectId);
        if (snapshot == nullptr || diff == nullptr
            || diff->targetSnapshotObjectId != snapshot->objectId
            || version.objectId != hashObject("commit", canonicalVersion(version))) {
            return false;
        }

        const ThinkingSpaceDocumentSnapshot* base = nullptr;
        if (index == 0) {
            if (hasPrunedHistory()) {
                if (version.parentObjectId != shallowBoundaryParentObjectId_) {
                    return false;
                }
            } else {
                if (!version.parentObjectId.empty()) {
                    return false;
                }
                base = &emptySnapshot;
            }
        } else {
            const ThinkingSpaceDocumentVersion& parent = versions_[index - 1];
            if (version.parentObjectId != parent.objectId) {
                return false;
            }
            base = findSnapshot(parent.snapshotObjectId);
            if (base == nullptr) {
                return false;
            }
        }

        if (base != nullptr) {
            if (diff->baseSnapshotObjectId != base->objectId) {
                return false;
            }
            const auto headerResult = diff->header.apply(canonicalHeader(base->headerMetadata));
            const auto bodyResult = diff->body.apply(base->bodyHtml);
            if (!headerResult.has_value() || !bodyResult.has_value()
                || *headerResult != canonicalHeader(snapshot->headerMetadata)
                || *bodyResult != snapshot->bodyHtml) {
                return false;
            }
        }
    }

    for (const auto& [objectId, snapshot] : snapshots_) {
        const bool referenced = std::ranges::any_of(versions_, [&](const auto& version) {
            return version.snapshotObjectId == objectId;
        });
        if (!referenced || objectId != snapshot.objectId || !snapshotMatchesId(snapshot)) {
            return false;
        }
    }
    for (const auto& [objectId, diff] : diffs_) {
        const bool referenced = std::ranges::any_of(versions_, [&](const auto& version) {
            return version.diffObjectId == objectId;
        });
        if (!referenced || objectId != diff.objectId
            || diff.objectId != hashObject("diff", canonicalDiff(diff))) {
            return false;
        }
    }
    return true;
}

void ThinkingSpaceDocumentVersionHistory::pruneOldestVersions()
{
    if (versions_.size() <= maximumVersionCount) {
        return;
    }

    const std::size_t overflow = versions_.size() - maximumVersionCount;
    versions_.erase(versions_.begin(), versions_.begin() + static_cast<std::ptrdiff_t>(overflow));
    prunedVersionCount_ += static_cast<std::uint64_t>(overflow);
    shallowBoundaryParentObjectId_ =
        versions_.empty() ? std::string{} : versions_.front().parentObjectId;
    collectUnreferencedObjects();
}

void ThinkingSpaceDocumentVersionHistory::collectUnreferencedObjects()
{
    std::erase_if(snapshots_, [&](const auto& entry) {
        return !std::ranges::any_of(versions_, [&](const auto& version) {
            return version.snapshotObjectId == entry.first;
        });
    });
    std::erase_if(diffs_, [&](const auto& entry) {
        return !std::ranges::any_of(versions_, [&](const auto& version) {
            return version.diffObjectId == entry.first;
        });
    });
}

ThinkingSpaceDocumentVersion ThinkingSpaceDocument::recordVersion(
    std::string label,
    std::string createdAtUtc)
{
    return versionHistory.record(
        header, body, std::move(label), std::move(createdAtUtc));
}

} // namespace ii::document
