#include "Metadata/Authorship_p.hpp"
#include "ThinkingSpace/ThinkingSpaceDocument.h"
#include <charconv>
#include <limits>
#include <nlohmann/json.hpp>

namespace ii::document {
namespace {
using Json = nlohmann::json;
const Json &object(const Json &value) {
    if (!value.is_object())
        throw DocumentError("Invalid Thinking Space object field");
    return value;
}
std::string string(const Json &value, const char *key) {
    object(value);
    auto i = value.find(key);
    if (i == value.end() || !i->is_string())
        throw DocumentError("Invalid Thinking Space string field");
    return i->get<std::string>();
}
std::map<std::string, std::string> map(const Json &value) {
    object(value);
    std::map<std::string, std::string> result;
    for (auto i = value.begin(); i != value.end(); ++i) {
        if (!i->is_string())
            throw DocumentError("Invalid Thinking Space metadata field");
        result.emplace(i.key(), i->get<std::string>());
    }
    return result;
}
std::uint64_t number(const Json &value, const char *key) {
    auto text = string(value, key);
    std::uint64_t result{};
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        std::to_string(result) != text)
        throw DocumentError("Invalid Thinking Space unsigned integer");
    return result;
}
const Json &array(const Json &value, const char *key, std::size_t maximum) {
    const auto &items = value.at(key);
    if (!items.is_array() || items.size() > maximum)
        throw DocumentError("Invalid Thinking Space collection");
    return items;
}
Json textDiff(const ThinkingSpaceTextDiff &diff) {
    return {{"prefix", std::to_string(diff.commonPrefixBytes)},
            {"suffix", std::to_string(diff.commonSuffixBytes)},
            {"removed", detail::encodeBase64(diff.removedText)},
            {"inserted", detail::encodeBase64(diff.insertedText)}};
}
ThinkingSpaceTextDiff readDiff(const Json &value, int schema) {
    object(value);
    auto prefix = number(value, "prefix"), suffix = number(value, "suffix");
    if (prefix > std::numeric_limits<std::size_t>::max() ||
        suffix > std::numeric_limits<std::size_t>::max())
        throw DocumentError("Thinking Space diff offset exceeds addressable size");
    auto removed = string(value, "removed"), inserted = string(value, "inserted");
    if (schema == 2) {
        removed = detail::decodeBase64(removed);
        inserted = detail::decodeBase64(inserted);
    }
    return {static_cast<std::size_t>(prefix), static_cast<std::size_t>(suffix), std::move(removed),
            std::move(inserted)};
}
} // namespace
const iiFileProvider::native::Authorship &ThinkingSpaceDocument::authorship() const noexcept {
    return body.htmlBlocks.authorship();
}
bool ThinkingSpaceDocument::setFileAuthor(const iiFileProvider::native::FileAuthor &author) {
    auto draft = *this;
    bool changed = draft.body.htmlBlocks.setFileAuthor(author);
    detail::storeAuthorship(draft.authorship(), draft.header.metadata);
    *this = std::move(draft);
    return changed;
}
bool ThinkingSpaceDocument::edit(const std::function<bool(ThinkingSpaceDocument &)> &callback) {
    if (!callback)
        throw DocumentError("Empty Thinking Space edit");
    auto draft = *this;
    if (!callback(draft))
        return false;
    bool changed = draft.header.metadata != header.metadata ||
                   draft.body.htmlBlocks.toFileBytes() != body.htmlBlocks.toFileBytes();
    if (changed && draft.authorship().dump() == authorship().dump())
        draft.body.htmlBlocks.recordChange();
    detail::storeAuthorship(draft.authorship(), draft.header.metadata);
    *this = std::move(draft);
    return changed;
}
std::string ThinkingSpaceDocument::toFileBytes() const {
    if (!versionHistory.verifyIntegrity())
        throw DocumentError("Invalid Thinking Space history");
    auto metadata = header.metadata;
    detail::storeAuthorship(authorship(), metadata);
    Json versions = Json::array(), snapshots = Json::array(), diffs = Json::array();
    for (const auto &v : versionHistory.versions_)
        versions.push_back({{"id", v.objectId},
                            {"parent", v.parentObjectId},
                            {"snapshot", v.snapshotObjectId},
                            {"diff", v.diffObjectId},
                            {"label", v.label},
                            {"createdAt", v.createdAtUtc}});
    for (const auto &[id, v] : versionHistory.snapshots_)
        snapshots.push_back({{"id", id},
                             {"headerId", v.headerObjectId},
                             {"bodyId", v.bodyObjectId},
                             {"header", v.headerMetadata},
                             {"bodyHtml", v.bodyHtml}});
    for (const auto &[id, v] : versionHistory.diffs_)
        diffs.push_back({{"id", id},
                         {"base", v.baseSnapshotObjectId},
                         {"target", v.targetSnapshotObjectId},
                         {"header", textDiff(v.header)},
                         {"body", textDiff(v.body)}});
    Json history{{"versions", versions},
                 {"snapshots", snapshots},
                 {"diffs", diffs},
                 {"pruned", std::to_string(versionHistory.prunedVersionCount_)},
                 {"boundary", versionHistory.shallowBoundaryParentObjectId_}};
    try {
        auto bytes = Json{
            {"format", "iiGeneralDocument.ThinkingSpace"},
            {"schemaVersion", 2},
            {"header", metadata},
            {"bodyHtml", body.htmlBlocks.html()},
            {"history",
             history}}.dump();
        if (bytes.size() > 64 * 1024 * 1024)
            throw DocumentError("Thinking Space file exceeds 64 MiB");
        return bytes;
    } catch (const Json::exception &e) {
        throw DocumentError(std::string("Invalid Thinking Space UTF-8: ") + e.what());
    }
}
ThinkingSpaceDocument ThinkingSpaceDocument::fromFileBytes(std::string_view bytes) {
    if (bytes.size() > 64 * 1024 * 1024)
        throw DocumentError("Thinking Space file exceeds 64 MiB");
    try {
        auto json = Json::parse(bytes);
        object(json);
        auto &schemaValue = json.at("schemaVersion");
        if (!schemaValue.is_number_integer())
            throw DocumentError("Invalid Thinking Space schema");
        int schema = schemaValue.get<int>();
        if (string(json, "format") != "iiGeneralDocument.ThinkingSpace" ||
            (schema != 1 && schema != 2))
            throw DocumentError("Unsupported Thinking Space file format");
        ThinkingSpaceDocument document;
        document.header.metadata = map(json.at("header"));
        auto author = detail::readAuthorship(document.header.metadata);
        document.body.htmlBlocks =
            HtmlBlockDocument::fromFileBytes(detail::markupBytes(string(json, "bodyHtml"), author));
        const auto &history = object(json.at("history"));
        auto &target = document.versionHistory;
        for (const auto &v : array(history, "versions", 100)) {
            object(v);
            target.versions_.push_back({string(v, "id"), string(v, "parent"), string(v, "snapshot"),
                                        string(v, "diff"), string(v, "label"),
                                        string(v, "createdAt")});
        }
        for (const auto &v : array(history, "snapshots", 100)) {
            object(v);
            auto id = string(v, "id");
            ThinkingSpaceDocumentSnapshot snapshot{id, string(v, "headerId"), string(v, "bodyId"),
                                                   map(v.at("header")), string(v, "bodyHtml")};
            (void)detail::readAuthorship(snapshot.headerMetadata);
            if (!target.snapshots_.emplace(id, std::move(snapshot)).second)
                throw DocumentError("Duplicate Thinking Space snapshot");
        }
        for (const auto &v : array(history, "diffs", 100)) {
            object(v);
            auto id = string(v, "id");
            ThinkingSpaceDocumentDiff diff{id, string(v, "base"), string(v, "target"),
                                           readDiff(v.at("header"), schema),
                                           readDiff(v.at("body"), schema)};
            if (!target.diffs_.emplace(id, std::move(diff)).second)
                throw DocumentError("Duplicate Thinking Space diff");
        }
        target.prunedVersionCount_ = number(history, "pruned");
        target.shallowBoundaryParentObjectId_ = string(history, "boundary");
        if (!target.verifyIntegrity())
            throw DocumentError("Thinking Space history integrity check failed");
        return document;
    } catch (const Json::exception &e) {
        throw DocumentError(std::string("Invalid Thinking Space JSON: ") + e.what());
    }
}
} // namespace ii::document
