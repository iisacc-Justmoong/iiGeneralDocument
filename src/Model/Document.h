#pragma once

#include "Core/PdfValue.h"
#include "Model/Element.h"
#include "Model/Page.h"
#include "Pdf/Objects.h"
#include "iiGeneralDocument/Export.h"
#include <iiFileProviderNative.h>

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ii::document {

class PdfDocumentReader;
class PdfDocumentWriter;
class DocumentEditor;

struct FormFieldId {
    std::uint64_t value{0};
    auto operator<=>(const FormFieldId&) const = default;
};

enum class FormFieldType {
    text,
    button,
    choice,
    signature,
    unknown,
};

class IIGENERALDOCUMENT_EXPORT FormField {
public:
    [[nodiscard]] FormFieldId id() const noexcept;
    void setId(FormFieldId id) noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    void setName(std::string utf8Name);
    [[nodiscard]] const std::string& originalName() const noexcept;
    void setOriginalName(std::string utf8Name);
    [[nodiscard]] FormFieldType type() const noexcept;
    void setType(FormFieldType type) noexcept;
    [[nodiscard]] const PdfValue& value() const noexcept;
    void setValue(PdfValue value);
    [[nodiscard]] const std::optional<std::string>& utf8Value() const noexcept;
    void setUtf8Value(std::string value);
    void clearUtf8Value() noexcept;
    [[nodiscard]] int flags() const noexcept;
    void setFlags(int flags) noexcept;
    [[nodiscard]] const std::optional<int>& maximumLength() const noexcept;
    void setMaximumLength(std::optional<int> maximum) noexcept;
    [[nodiscard]] const std::vector<std::string>& choices() const noexcept;
    void setChoices(std::vector<std::string> choices);
    [[nodiscard]] const std::optional<SourceReference>& source() const noexcept;
    void setSource(std::optional<SourceReference> source) noexcept;

private:
    FormFieldId id_;
    std::string name_;
    std::string originalName_;
    FormFieldType type_{FormFieldType::unknown};
    PdfValue value_;
    std::optional<std::string> utf8Value_;
    int flags_{0};
    std::optional<int> maximumLength_;
    std::vector<std::string> choices_;
    std::optional<SourceReference> source_;
};

class IIGENERALDOCUMENT_EXPORT Document {
public:
    Document();
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    [[nodiscard]] const std::vector<Page>& pages() const noexcept;
    [[nodiscard]] std::vector<Page>& pages() noexcept;
    void addPage(Page page);

    [[nodiscard]] const iiFileProvider::native::Authorship& authorship() const noexcept;
    bool setFileAuthor(const iiFileProvider::native::FileAuthor& author);
    bool setMetadata(std::string key, std::string value);
    bool removeMetadata(std::string_view key);
    // For legacy direct aggregate edits, call once after a successful change.
    void recordChange();
    // Reader boundary: validates stored metadata and clears the active identity.
    void restoreAuthorship();

    [[nodiscard]] const std::map<std::string, std::string>& metadata() const noexcept;
    [[nodiscard]] std::map<std::string, std::string>& metadata() noexcept;

    [[nodiscard]] const std::vector<FormField>& formFields() const noexcept;
    [[nodiscard]] std::vector<FormField>& formFields() noexcept;
    void addFormField(FormField field);

    [[nodiscard]] const std::string& pdfVersion() const noexcept;
    void setPdfVersion(std::string version);
    [[nodiscard]] bool isEncrypted() const noexcept;
    [[nodiscard]] bool hasDigitalSignatures() const noexcept;
    [[nodiscard]] bool hasXfa() const noexcept;
    [[nodiscard]] const std::vector<pdf::Attachment>& attachments() const noexcept;
    [[nodiscard]] bool hasSourcePdf() const noexcept;

private:
    struct Origin {
        std::vector<char> bytes;
        std::string description;
        std::string password;
        bool encrypted{false};
        bool digitallySigned{false};
        bool xfa{false};
    };

    std::vector<Page> pages_;
    std::map<std::string, std::string> metadata_;
    iiFileProvider::native::Authorship authorship_;
    std::vector<FormField> formFields_;
    std::vector<pdf::Attachment> attachments_;
    std::string pdfVersion_{"1.7"};
    std::optional<Origin> origin_;

    friend class PdfDocumentReader;
    friend class PdfDocumentWriter;
    friend class DocumentEditor;
};

} // namespace ii::document
