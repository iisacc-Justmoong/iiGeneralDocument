#pragma once

#include "Html/HtmlBlockDocument.h"
#include "iiGeneralDocument/Export.h"

#include <map>
#include <string>
#include <string_view>

namespace ii::document {

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocumentHeader {
    std::map<std::string, std::string> metadata;
};

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocumentBody {
    HtmlBlockDocument htmlBlocks;
};

struct IIGENERALDOCUMENT_EXPORT ThinkingSpaceDocument {
    static constexpr std::string_view fileExtension{".tsdoc"};

    ThinkingSpaceDocumentHeader header;
    ThinkingSpaceDocumentBody body;
};

} // namespace ii::document
