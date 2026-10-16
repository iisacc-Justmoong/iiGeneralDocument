#pragma once
#include "Codec/Private/Base64.h"
#include <algorithm>
#include <iiFileProviderNative.h>
#include <map>
namespace ii::document::detail {
inline void storeAuthorship(const iiFileProvider::native::Authorship &author,
                            std::map<std::string, std::string> &metadata) {
    auto next = metadata;
    if (author.isEmpty())
        next.erase(iiFileProvider::native::Authorship::MetadataKey);
    else
        next[iiFileProvider::native::Authorship::MetadataKey] = author.dump();
    metadata.swap(next);
}
inline iiFileProvider::native::Authorship
readAuthorship(const std::map<std::string, std::string> &metadata) {
    auto found = metadata.find(iiFileProvider::native::Authorship::MetadataKey);
    if (found == metadata.end())
        return {};
    auto parsed = iiFileProvider::native::Authorship::fromDump(found->second);
    if (!parsed)
        throw DocumentError("Invalid file authorship metadata");
    return std::move(*parsed);
}
inline std::string markupBytes(const std::string &body,
                               const iiFileProvider::native::Authorship &author) {
    return author.isEmpty()
               ? body
               : body + "\n<!--iisacc:authorship:v1:" + encodeBase64(author.dump()) + "-->";
}
struct AuthoredMarkup {
    std::string body;
    iiFileProvider::native::Authorship author;
};
inline AuthoredMarkup readMarkup(std::string_view bytes) {
    constexpr std::string_view marker = "\n<!--iisacc:authorship:v1:";
    auto begin = bytes.find(marker);
    if (begin == bytes.npos) {
        if (bytes.find("<!--iisacc:authorship:") != bytes.npos)
            throw DocumentError("Unsupported authorship comment");
        return {std::string(bytes), {}};
    }
    auto end = bytes.find("-->", begin + marker.size());
    auto space = [](char c) {
        return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\f';
    };
    if (end == bytes.npos || !std::ranges::all_of(bytes.substr(end + 3), space) ||
        bytes.find("<!--iisacc:authorship:", begin + marker.size()) != bytes.npos ||
        bytes.substr(0, begin).find("<!--iisacc:authorship:") != bytes.npos)
        throw DocumentError("Malformed or duplicate authorship comment");
    auto encoded = bytes.substr(begin + marker.size(), end - begin - marker.size());
    if (encoded.size() > iiFileProvider::native::Authorship::MaximumBytes * 2)
        throw DocumentError("Oversized authorship comment");
    auto author = iiFileProvider::native::Authorship::fromDump(decodeBase64(encoded));
    if (!author)
        throw DocumentError("Invalid authorship comment metadata");
    return {std::string(bytes.substr(0, begin)), std::move(*author)};
}
} // namespace ii::document::detail
