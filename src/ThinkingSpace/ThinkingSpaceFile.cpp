#include "ThinkingSpace/ThinkingSpaceDocument.h"
#include "Metadata/Authorship_p.hpp"
#include <QJsonDocument>
#include <limits>

namespace ii::document {
namespace {
QString q(const std::string &value) {
    auto result = QString::fromStdString(value);
    if (result.toUtf8().toStdString() != value) throw DocumentError("Thinking Space file strings must be valid UTF-8");
    return result;
}
QJsonObject object(const std::map<std::string,std::string> &map) {
    QJsonObject json; for (const auto &[key,value] : map) json[q(key)] = q(value); return json;
}
std::string string(const QJsonObject &json, const char *key) {
    const auto value = json.value(QLatin1String(key));
    if (!value.isString()) throw DocumentError("Invalid Thinking Space string field");
    return value.toString().toStdString();
}
QJsonObject requiredObject(const QJsonValue &value) {
    if (!value.isObject()) throw DocumentError("Invalid Thinking Space object field");
    return value.toObject();
}
std::map<std::string,std::string> map(const QJsonValue &value) {
    const auto json = requiredObject(value); std::map<std::string,std::string> result;
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (!it.value().isString()) throw DocumentError("Invalid Thinking Space metadata field");
        result[it.key().toStdString()] = it.value().toString().toStdString();
    }
    return result;
}
quint64 number(const QJsonObject &json,const char *key) {
    const auto text = QString::fromStdString(string(json,key)); bool ok = false;
    const auto value = text.toULongLong(&ok);
    if (!ok || QString::number(value)!=text) throw DocumentError("Invalid Thinking Space unsigned integer");
    return value;
}
QJsonArray array(const QJsonObject &json,const char *key, qsizetype max) {
    const auto value = json.value(QLatin1String(key));
    if (!value.isArray() || value.toArray().size()>max) throw DocumentError("Invalid Thinking Space collection");
    return value.toArray();
}
QJsonObject textDiff(const ThinkingSpaceTextDiff &diff) {
    return {{"prefix",QString::number(diff.commonPrefixBytes)}, {"suffix",QString::number(diff.commonSuffixBytes)},
            {"removed",q(diff.removedText)}, {"inserted",q(diff.insertedText)}};
}
ThinkingSpaceTextDiff readDiff(const QJsonValue &value) {
    const auto json = requiredObject(value); const auto prefix = number(json,"prefix"), suffix = number(json,"suffix");
    if (prefix>std::numeric_limits<std::size_t>::max() || suffix>std::numeric_limits<std::size_t>::max())
        throw DocumentError("Thinking Space diff offset exceeds addressable size");
    return {static_cast<std::size_t>(prefix),static_cast<std::size_t>(suffix),string(json,"removed"),string(json,"inserted")};
}
}
const iiFileProvider::Authorship& ThinkingSpaceDocument::authorship() const noexcept { return body.htmlBlocks.authorship(); }
bool ThinkingSpaceDocument::setFileAuthor(const iiFileProvider::FileAuthor& author) {
    auto draft = *this; const bool changed = draft.body.htmlBlocks.setFileAuthor(author);
    detail::storeAuthorship(draft.authorship(), draft.header.metadata); *this = std::move(draft); return changed;
}
bool ThinkingSpaceDocument::edit(const std::function<bool(ThinkingSpaceDocument&)>& callback) {
    if (!callback) throw DocumentError("Empty Thinking Space edit");
    auto draft = *this; if (!callback(draft)) return false;
    // Body editors already stamp their own changes. Header-only edits stamp here.
    const bool changed = draft.header.metadata != header.metadata || draft.body.htmlBlocks.toFileBytes() != body.htmlBlocks.toFileBytes();
    if (changed && draft.authorship().dump() == authorship().dump()) draft.body.htmlBlocks.recordChange();
    detail::storeAuthorship(draft.authorship(),draft.header.metadata); *this = std::move(draft); return changed;
}
QByteArray ThinkingSpaceDocument::toFileBytes() const {
    if (!versionHistory.verifyIntegrity()) throw DocumentError("Invalid Thinking Space history");
    auto metadata = header.metadata; detail::storeAuthorship(authorship(),metadata);
    QJsonArray versions,snapshots,diffs;
    for (const auto &v : versionHistory.versions_) versions.append(QJsonObject{
        {"id",q(v.objectId)}, {"parent",q(v.parentObjectId)}, {"snapshot",q(v.snapshotObjectId)},
        {"diff",q(v.diffObjectId)}, {"label",q(v.label)}, {"createdAt",q(v.createdAtUtc)}});
    for (const auto &[id,v] : versionHistory.snapshots_) snapshots.append(QJsonObject{
        {"id",q(id)}, {"headerId",q(v.headerObjectId)}, {"bodyId",q(v.bodyObjectId)},
        {"header",object(v.headerMetadata)}, {"bodyHtml",q(v.bodyHtml)}});
    for (const auto &[id,v] : versionHistory.diffs_) diffs.append(QJsonObject{
        {"id",q(id)}, {"base",q(v.baseSnapshotObjectId)}, {"target",q(v.targetSnapshotObjectId)},
        {"header",textDiff(v.header)}, {"body",textDiff(v.body)}});
    const QJsonObject history{{"versions",versions}, {"snapshots",snapshots}, {"diffs",diffs},
        {"pruned",QString::number(versionHistory.prunedVersionCount_)},
        {"boundary",q(versionHistory.shallowBoundaryParentObjectId_)}};
    const auto bytes = QJsonDocument(QJsonObject{{"format","iiGeneralDocument.ThinkingSpace"},
        {"schemaVersion",1}, {"header",object(metadata)}, {"bodyHtml",q(body.htmlBlocks.html())},
        {"history",history}}).toJson(QJsonDocument::Compact);
    if (bytes.size()>64*1024*1024) throw DocumentError("Thinking Space file exceeds 64 MiB");
    return bytes;
}
ThinkingSpaceDocument ThinkingSpaceDocument::fromFileBytes(const QByteArray &bytes) {
    if (bytes.size()>64*1024*1024) throw DocumentError("Thinking Space file exceeds 64 MiB");
    QJsonParseError error; const auto parsed = QJsonDocument::fromJson(bytes,&error);
    if (error.error!=QJsonParseError::NoError || !parsed.isObject()) throw DocumentError("Invalid Thinking Space JSON");
    const auto json = parsed.object();
    if (string(json,"format")!="iiGeneralDocument.ThinkingSpace" || json.value("schemaVersion")!=QJsonValue(1))
        throw DocumentError("Unsupported Thinking Space file format");
    ThinkingSpaceDocument document; document.header.metadata = map(json.value("header"));
    const auto author = detail::readAuthorship(document.header.metadata);
    document.body.htmlBlocks = HtmlBlockDocument::fromFileBytes(detail::markupBytes(string(json,"bodyHtml"),author));
    const auto history = requiredObject(json.value("history"));
    auto &target = document.versionHistory;
    for (const auto &value : array(history,"versions",100)) {
        const auto v = requiredObject(value);
        target.versions_.push_back({string(v,"id"),string(v,"parent"),string(v,"snapshot"),string(v,"diff"),string(v,"label"),string(v,"createdAt")});
    }
    for (const auto &value : array(history,"snapshots",100)) {
        const auto v = requiredObject(value); const auto id = string(v,"id");
        ThinkingSpaceDocumentSnapshot snapshot{id,string(v,"headerId"),string(v,"bodyId"),map(v.value("header")),string(v,"bodyHtml")};
        (void)detail::readAuthorship(snapshot.headerMetadata);
        if (!target.snapshots_.emplace(id,std::move(snapshot)).second) throw DocumentError("Duplicate Thinking Space snapshot");
    }
    for (const auto &value : array(history,"diffs",100)) {
        const auto v = requiredObject(value); const auto id = string(v,"id");
        ThinkingSpaceDocumentDiff diff{id,string(v,"base"),string(v,"target"),readDiff(v.value("header")),readDiff(v.value("body"))};
        if (!target.diffs_.emplace(id,std::move(diff)).second) throw DocumentError("Duplicate Thinking Space diff");
    }
    target.prunedVersionCount_ = number(history,"pruned");
    target.shallowBoundaryParentObjectId_ = string(history,"boundary");
    if (!target.verifyIntegrity()) throw DocumentError("Thinking Space history integrity check failed");
    return document;
}
} // namespace ii::document
