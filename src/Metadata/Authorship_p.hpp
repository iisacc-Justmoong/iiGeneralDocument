#pragma once
#include "Core/Diagnostic.h"
#include <iiFileProvider.h>
#include <map>
#include <string>

namespace ii::document::detail {
inline void storeAuthorship(const iiFileProvider::Authorship &author, std::map<std::string,std::string> &metadata) {
    auto next = metadata;
    if (author.isEmpty()) next.erase(iiFileProvider::Authorship::MetadataKey);
    else next[iiFileProvider::Authorship::MetadataKey] = author.dump().toStdString();
    metadata.swap(next);
}
inline iiFileProvider::Authorship readAuthorship(const std::map<std::string,std::string> &metadata) {
    const auto found = metadata.find(iiFileProvider::Authorship::MetadataKey);
    if (found == metadata.end()) return {};
    auto parsed = iiFileProvider::Authorship::fromDump(QByteArray::fromStdString(found->second));
    if (!parsed) throw DocumentError("Invalid file authorship metadata");
    return std::move(*parsed);
}
inline QByteArray markupBytes(const std::string &body, const iiFileProvider::Authorship &author) {
    auto bytes = QByteArray::fromStdString(body);
    if (!author.isEmpty()) bytes += "\n<!--iisacc:authorship:v1:" + author.dump().toBase64() + "-->";
    return bytes;
}
struct AuthoredMarkup { std::string body; iiFileProvider::Authorship author; };
inline AuthoredMarkup readMarkup(const QByteArray &bytes) {
    const QByteArray marker = "\n<!--iisacc:authorship:v1:";
    const auto begin = bytes.indexOf(marker);
    if (begin < 0) {
        if (bytes.contains("<!--iisacc:authorship:")) throw DocumentError("Unsupported authorship comment");
        return {bytes.toStdString(), {}};
    }
    const auto end = bytes.indexOf("-->",begin+marker.size());
    if (end < 0 || !bytes.mid(end+3).trimmed().isEmpty() || bytes.indexOf(marker,begin+1)>=0)
        throw DocumentError("Malformed or duplicate authorship comment");
    const auto encoded = bytes.mid(begin+marker.size(),end-begin-marker.size());
    if (encoded.size() > iiFileProvider::Authorship::MaximumBytes*2) throw DocumentError("Oversized authorship comment");
    const auto decoded = QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.toBase64()!=encoded) throw DocumentError("Invalid authorship comment encoding");
    auto author = iiFileProvider::Authorship::fromDump(decoded.decoded);
    if (!author) throw DocumentError("Invalid authorship comment metadata");
    return {bytes.first(begin).toStdString(), std::move(*author)};
}
}
