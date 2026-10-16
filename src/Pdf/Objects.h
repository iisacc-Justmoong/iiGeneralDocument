#pragma once

#include "iiGeneralDocument/PdfExport.h"
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ii::document::pdf {

enum class ErrorCode { invalidInput, malformedPdf, encryptedInput, signedInput,
    unsupportedFeature, verificationFailed, backendFailure };
struct Error { ErrorCode code{}; std::string message; };
template<class T> using Result = std::expected<T,Error>;
using Bytes = std::vector<std::byte>;
struct Box { double x0{},y0{},x1{},y1{}; };
struct Annotation {
    std::string subtype;
    std::string contents;
    std::optional<Box> rectangle;
    std::string author;
};
struct Page {
    std::size_t index{}; // Zero based, like the existing SDK page/editor APIs.
    Box mediaBox;
    Box cropBox;
    int clockwiseRotation{};
    double userUnit{1.0};
    std::vector<Annotation> annotations;
};
struct FormField {
    std::string name,type,value;
    std::uint32_t flags{};
    std::optional<int> maximumLength;
};
struct Attachment {
    std::string key,filename,description,mimeType;
    std::optional<std::uint64_t> declaredSize;
};
struct MetadataChange { std::string key; std::optional<std::string> value; };
class Document;
class Editor;
struct PageSelection {
    std::reference_wrapper<const Document> document;
    std::vector<std::size_t> pages; // Empty means all. Order/repetitions are retained.
};

// Format object: owns PDF bytes and readonly structure snapshots. File CRUD and
// application authorship belong to the caller/Document aggregate, not this codec.
class IIGENERALDOCUMENT_PDF_EXPORT Document final {
public:
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    [[nodiscard]] static Result<Document> fromBytes(std::span<const std::byte> bytes);
    [[nodiscard]] static Result<Document> merge(std::span<const PageSelection> selections);
    [[nodiscard]] Result<Document> extractPages(std::span<const std::size_t> pages) const;
    [[nodiscard]] Bytes toBytes() const;
    [[nodiscard]] const std::vector<Page>& pages() const;
    [[nodiscard]] const std::vector<FormField>& formFields() const;
    [[nodiscard]] const std::vector<Attachment>& attachments() const;
    [[nodiscard]] const std::map<std::string,std::string>& metadata() const;
    [[nodiscard]] std::string_view version() const;
    [[nodiscard]] bool hasDigitalSignatures() const;
    [[nodiscard]] bool hasXfa() const;
    [[nodiscard]] bool needsAppearances() const;
    [[nodiscard]] const std::vector<std::string>& warnings() const;
private:
    struct Impl;
    explicit Document(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
    friend class Editor;
};

// Successful edits atomically replace the owned bytes/snapshots. Rejected edits
// leave the object unchanged. Snapshot references expire after a successful edit.
class IIGENERALDOCUMENT_PDF_EXPORT Editor final {
public:
    explicit Editor(Document& document) noexcept;
    [[nodiscard]] Result<void> rotatePages(std::span<const std::size_t> pages,int clockwiseDegrees);
    [[nodiscard]] Result<void> setMetadata(std::span<const MetadataChange> changes);
    [[nodiscard]] Result<void> addTextNote(std::size_t page,Box rectangle,
        std::string_view contents,std::string_view author = {});
    [[nodiscard]] Result<void> setTextField(std::string_view name,std::string_view value);
private:
    Document& document_;
};

} // namespace ii::document::pdf
